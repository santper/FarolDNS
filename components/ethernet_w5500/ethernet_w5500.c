#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_eth_mac_spi.h"
#include "esp_eth_phy.h"
#include "ethernet_w5500.h"
#include "storage_manager.h"
#include "esp_mac.h"

// Note: lwip/ip_addr.h is required for ipaddr_addr
#include "lwip/ip_addr.h"

static const char *TAG = "EthernetW5500";
static bool s_connected = false;
static esp_netif_t *s_eth_netif = NULL;
static esp_eth_handle_t s_eth_handle = NULL;

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Up");
        s_connected = true;
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        s_connected = false;
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        s_connected = false;
        break;
    default:
        break;
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet adquiriu IP com sucesso!");
    ESP_LOGI(TAG, "IP:      " IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&ip_info->gw));

    // Captura o IP obtido via DHCP como IP fixo na config
    faroldns_config_t cfg;
    if (storage_load_config(&cfg) == ESP_OK && cfg.dhcp_enabled) {
        snprintf(cfg.ip, sizeof(cfg.ip), IPSTR, IP2STR(&ip_info->ip));
        snprintf(cfg.netmask, sizeof(cfg.netmask), IPSTR, IP2STR(&ip_info->netmask));
        snprintf(cfg.gw, sizeof(cfg.gw), IPSTR, IP2STR(&ip_info->gw));
        cfg.dhcp_enabled = false;
        storage_save_config(&cfg);
        ESP_LOGI(TAG, "IP DHCP capturado como IP fixo: %s", cfg.ip);
    }
}

esp_err_t eth_w5500_init(void)
{
    ESP_LOGI(TAG, "Inicializando Ethernet W5500...");

    // Carrega configuracao
    faroldns_config_t config;
    if (storage_load_config(&config) != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao carregar config. Usando padroes.");
        storage_get_default_config(&config);
    }

    // Registra tratadores de eventos
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    // Cria interface netif para Ethernet SPI
    // NOTA: usa "ETH_DEF" (if_key padrao) para compatibilidade com o mDNS do ESP-IDF,
    // que procura "ETH_DEF" via esp_netif_get_handle_from_ifkey()
    esp_netif_inherent_config_t eth_behav_cfg = ESP_NETIF_INHERENT_DEFAULT_ETH();
    eth_behav_cfg.if_desc = "eth0";
    eth_behav_cfg.route_prio = 50;
    
    esp_netif_config_t eth_netif_config = {
        .base = &eth_behav_cfg,
        .driver = NULL,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
    };
    s_eth_netif = esp_netif_new(&eth_netif_config);

    // Se IP estatico estiver configurado, para DHCP client e define IP
    if (!config.dhcp_enabled) {
        ESP_LOGI(TAG, "Configurando IP estatico para Ethernet...");
        esp_netif_dhcpc_stop(s_eth_netif);
        
        esp_netif_ip_info_t ip_info;
        memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));
        ip_info.ip.addr = ipaddr_addr(config.ip);
        ip_info.netmask.addr = ipaddr_addr(config.netmask);
        ip_info.gw.addr = ipaddr_addr(config.gw);
        
        esp_netif_set_ip_info(s_eth_netif, &ip_info);
    }

    // Inicializa barramento SPI
    ESP_LOGI(TAG, "Configurando barramento SPI para W5500...");
    spi_bus_config_t buscfg = {
        .miso_io_num = ETH_PIN_MISO,
        .mosi_io_num = ETH_PIN_MOSI,
        .sclk_io_num = ETH_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    // SPI2_HOST
    esp_err_t ret = spi_bus_initialize(ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE significa que o barramento SPI ja foi inicializado
        ESP_LOGE(TAG, "Falha ao inicializar SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configura o dispositivo SPI do W5500
    spi_device_interface_config_t devcfg = {
        .command_bits = 16,
        .address_bits = 8,
        .mode = 0,
        .clock_speed_hz = 20 * 1000 * 1000, // 20 MHz
        .spics_io_num = ETH_PIN_CS,
        .queue_size = 20
    };

    // Configuracoes fisicas (MAC e PHY) do W5500
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = 1;
    phy_config.reset_gpio_num = ETH_PIN_RST;

    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(ETH_SPI_HOST, &devcfg);
    // Nota: Como estamos em modo Polling conforme acordado anteriormente, 
    // a interrupcao fisica INT nao e estritamente vinculada pelo driver para eventos,
    // mas a struct do driver do ESP-IDF requer definir int_gpio_num.
    w5500_config.int_gpio_num = ETH_PIN_INT; 
    // Inicializa o servico de interrupcao GPIO
    esp_err_t isr_ret = gpio_install_isr_service(0);
    if (isr_ret != ESP_OK && isr_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Falha ao instalar GPIO ISR service: %s", esp_err_to_name(isr_ret));
        return isr_ret;
    }

    ESP_LOGI(TAG, "Criando MAC e PHY...");
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &s_eth_handle));

    // Define o endereço MAC gerado a partir do eFuse do ESP32 para a interface Ethernet
    uint8_t mac_addr[6] = {0};
    ESP_ERROR_CHECK(esp_read_mac(mac_addr, ESP_MAC_ETH));
    ESP_ERROR_CHECK(esp_eth_ioctl(s_eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr));

    ESP_ERROR_CHECK(esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(s_eth_handle)));
    ESP_ERROR_CHECK(esp_eth_start(s_eth_handle));

    return ESP_OK;
}

bool eth_w5500_is_connected(void)
{
    return s_connected;
}

esp_netif_t* eth_w5500_get_netif(void)
{
    return s_eth_netif;
}
