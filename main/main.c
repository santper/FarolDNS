#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_eth_mac_spi.h"
#include "esp_eth_phy.h"

// --- CONFIGURAÇÃO DE PINOS DO W5500 ---
// Ajuste os GPIOs de acordo com sua fiação física do ESP32-S3 para o W5500
#define PIN_MISO 13
#define PIN_MOSI 11
#define PIN_SCLK 12
#define PIN_CS   10
#define PIN_INT  4
#define PIN_RST  5

#define SPI_HOST_ID SPI2_HOST

// --- CONFIGURAÇÃO DNS ---
#define LOCAL_DNS_PORT 53
#define UPSTREAM_DNS_IP "1.1.1.1"
#define MAX_DNS_PACKET_SIZE 512

static const char *TAG = "FarolDNS";
static bool s_ethernet_connected = false;

// Manipulador de eventos de rede (Ethernet)
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Up");
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        s_ethernet_connected = false;
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        s_ethernet_connected = false;
        break;
    default:
        break;
    }
}

// Manipulador de eventos de obtenção de IP
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet adquiriu endereço IP com sucesso!");
    ESP_LOGI(TAG, "~~~~~~~~~~~ Network Info ~~~~~~~~~~~");
    ESP_LOGI(TAG, "IP:      " IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
    s_ethernet_connected = true;
}

// Tarefa executora do servidor DNS Forwarder
static void dns_forwarder_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Aguardando conexão Ethernet para iniciar o DNS Forwarder...");
    while (!s_ethernet_connected) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "Iniciando socket do DNS Forwarder...");

    // 1. Criação do socket UDP do servidor
    int server_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (server_sock < 0) {
        ESP_LOGE(TAG, "Falha ao criar socket do servidor: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(LOCAL_DNS_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // 2. Vincula (bind) o socket à porta local 53
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Falha ao vincular (bind) na porta %d: errno %d", LOCAL_DNS_PORT, errno);
        close(server_sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Servidor DNS ouvindo na porta %d", LOCAL_DNS_PORT);

    uint8_t rx_buffer[MAX_DNS_PACKET_SIZE];
    uint8_t tx_buffer[MAX_DNS_PACKET_SIZE];

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t socklen = sizeof(client_addr);
        
        // 3. Recebe query DNS de um cliente
        int len = recvfrom(server_sock, rx_buffer, sizeof(rx_buffer), 0,
                           (struct sockaddr *)&client_addr, &socklen);
        if (len < 0) {
            ESP_LOGE(TAG, "recvfrom falhou: errno %d", errno);
            continue;
        }

        char client_ip_str[32];
        inet_ntoa_r(client_addr.sin_addr, client_ip_str, sizeof(client_ip_str));
        ESP_LOGI(TAG, "Consulta DNS recebida de %s:%d (%d bytes)", 
                 client_ip_str, ntohs(client_addr.sin_port), len);

        // 4. Cria socket UDP cliente dinâmico para falar com o DNS Upstream
        int client_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (client_sock < 0) {
            ESP_LOGE(TAG, "Falha ao criar socket de cliente: errno %d", errno);
            continue;
        }

        // Configura timeout de 2 segundos para o recebimento da resposta
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_in upstream_addr;
        upstream_addr.sin_family = AF_INET;
        upstream_addr.sin_port = htons(53);
        upstream_addr.sin_addr.s_addr = inet_addr(UPSTREAM_DNS_IP);

        // 5. Encaminha a query DNS para o servidor público (ex: 1.1.1.1)
        ESP_LOGI(TAG, "Encaminhando consulta para o DNS público %s...", UPSTREAM_DNS_IP);
        int sent = sendto(client_sock, rx_buffer, len, 0,
                          (struct sockaddr *)&upstream_addr, sizeof(upstream_addr));
        if (sent < 0) {
            ESP_LOGE(TAG, "Falha ao enviar para o DNS público: errno %d", errno);
            close(client_sock);
            continue;
        }

        // 6. Aguarda e recebe a resposta do DNS público
        struct sockaddr_in reply_addr;
        socklen_t reply_socklen = sizeof(reply_addr);
        int reply_len = recvfrom(client_sock, tx_buffer, sizeof(tx_buffer), 0,
                                 (struct sockaddr *)&reply_addr, &reply_socklen);

        if (reply_len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                ESP_LOGW(TAG, "Timeout: O DNS público não respondeu a tempo.");
            } else {
                ESP_LOGE(TAG, "Erro ao receber dados do DNS público: errno %d", errno);
            }
        } else {
            ESP_LOGI(TAG, "Resposta recebida do DNS público (%d bytes). Enviando ao cliente...", reply_len);
            
            // 7. Envia a resposta de volta ao cliente original
            int sent_back = sendto(server_sock, tx_buffer, reply_len, 0,
                                  (struct sockaddr *)&client_addr, socklen);
            if (sent_back < 0) {
                ESP_LOGE(TAG, "Falha ao responder ao cliente: errno %d", errno);
            } else {
                ESP_LOGI(TAG, "Resposta DNS encaminhada com sucesso para o cliente.");
            }
        }

        close(client_sock);
    }

    close(server_sock);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "[FarolDNS] Inicializando sistema...");

    // Inicialização da partição NVS (Necessária no ESP-IDF para inicializar subsistemas de rede)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 1. Inicializa o loop de eventos padrão e pilha TCP/IP (lwIP)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 2. Registra tratadores de eventos para rede e aquisição de IP
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));

    // 3. Cria a interface netif padrão do lwIP para Ethernet SPI
    esp_netif_inherent_config_t eth_behav_cfg = ESP_NETIF_INHERENT_DEFAULT_ETH();
    eth_behav_cfg.if_key = "ETH_SPI_5500";
    eth_behav_cfg.if_desc = "eth0";
    eth_behav_cfg.route_prio = 50;
    
    esp_netif_config_t eth_netif_config = {
        .base = &eth_behav_cfg,
        .driver = NULL,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH
    };
    esp_netif_t *eth_netif = esp_netif_new(&eth_netif_config);

    // 4. Inicialização do barramento SPI (SPI2_HOST é o padrão do ESP32-S3)
    ESP_LOGI(TAG, "Configurando barramento SPI...");
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_MISO,
        .mosi_io_num = PIN_MOSI,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST_ID, &buscfg, SPI_DMA_CH_AUTO));

    // 5. Configurações do dispositivo SPI para o W5500
    spi_device_interface_config_t devcfg = {
        .command_bits = 0,
        .address_bits = 8,
        .mode = 0,
        .clock_speed_hz = 20 * 1000 * 1000, // 20 MHz (teste seguro para fiação jumper)
        .spics_io_num = PIN_CS,
        .queue_size = 20
    };

    // 6. Configurações físicas (MAC e PHY) do W5500
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = 1; // Endereço PHY padrão do W5500
    phy_config.reset_gpio_num = PIN_RST;

    // A macro ETH_W5500_DEFAULT_CONFIG recebe o Host SPI e o ponteiro para a config do dispositivo
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(SPI_HOST_ID, &devcfg);
    w5500_config.int_gpio_num = PIN_INT;

    ESP_LOGI(TAG, "Criando instâncias do MAC e PHY do W5500...");
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);

    esp_eth_handle_t eth_handle = NULL;
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    
    ESP_LOGI(TAG, "Instalando driver Ethernet (esp_eth)...");
    ESP_ERROR_CHECK(esp_eth_driver_install(&config, &eth_handle));

    // 7. Vincula a interface netif do lwIP ao driver Ethernet
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));

    // 8. Inicia o driver Ethernet (Link Negotiation / Deteção de cabo)
    ESP_LOGI(TAG, "Iniciando Ethernet...");
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));

    // 9. Dispara a tarefa em segundo plano que executa o servidor DNS Forwarder
    xTaskCreate(dns_forwarder_task, "dns_forwarder_task", 4096, NULL, 5, NULL);
}
