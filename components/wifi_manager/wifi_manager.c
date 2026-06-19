#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "wifi_manager.h"
#include "storage_manager.h"
#include "lwip/ip_addr.h"

static const char *TAG = "WiFiManager";
static bool s_connected = false;
static esp_netif_t *s_wifi_netif = NULL;
static bool s_wifi_initialized = false;
static wifi_mode_t s_current_mode = WIFI_MODE_NULL;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi Station iniciado. Conectando...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Wi-Fi Station desconectado. Tentando reconectar...");
        s_connected = false;
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        const esp_netif_ip_info_t *ip_info = &event->ip_info;
        ESP_LOGI(TAG, "Wi-Fi adquiriu IP com sucesso!");
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ip_info->ip));
        s_connected = true;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        ESP_LOGI(TAG, "Wi-Fi Access Point (AP) iniciado. SSID: %s", WIFI_AP_SSID);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Cliente conectado ao AP. MAC: " MACSTR, MAC2STR(event->mac));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Cliente desconectou do AP. MAC: " MACSTR, MAC2STR(event->mac));
    }
}

esp_err_t wifi_manager_init(void)
{
    if (s_wifi_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Inicializando Wi-Fi base...");
    
    // Registra tratadores de eventos de Wi-Fi e IP
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    s_wifi_initialized = true;
    return ESP_OK;
}

esp_err_t wifi_manager_start_ap(void)
{
    ESP_ERROR_CHECK(wifi_manager_init());

    if (s_current_mode == WIFI_MODE_AP) {
        return ESP_OK;
    }

    if (s_current_mode != WIFI_MODE_NULL) {
        wifi_manager_stop();
    }

    ESP_LOGI(TAG, "Iniciando Wi-Fi no modo Access Point (AP)...");

    s_wifi_netif = esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_current_mode = WIFI_MODE_AP;
    return ESP_OK;
}

esp_err_t wifi_manager_start_sta(void)
{
    ESP_ERROR_CHECK(wifi_manager_init());

    faroldns_config_t config;
    if (storage_load_config(&config) != ESP_OK) {
        storage_get_default_config(&config);
    }

    if (strlen(config.wifi_ssid) == 0) {
        ESP_LOGW(TAG, "SSID do Wi-Fi vazio nas configuracoes. Iniciando AP de Fallback.");
        return wifi_manager_start_ap();
    }

    if (s_current_mode == WIFI_MODE_STA) {
        return ESP_OK;
    }

    if (s_current_mode != WIFI_MODE_NULL) {
        wifi_manager_stop();
    }

    ESP_LOGI(TAG, "Iniciando Wi-Fi no modo Station (STA) para conectar a: %s...", config.wifi_ssid);

    s_wifi_netif = esp_netif_create_default_wifi_sta();

    // Se IP estatico estiver configurado para o Wi-Fi, desativa DHCP client e define IP
    if (!config.wifi_dhcp && strlen(config.wifi_ip) > 0) {
        ESP_LOGI(TAG, "Configurando IP estatico para Wi-Fi...");
        esp_netif_dhcpc_stop(s_wifi_netif);
        
        esp_netif_ip_info_t ip_info;
        memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));
        ip_info.ip.addr = ipaddr_addr(config.wifi_ip);
        ip_info.netmask.addr = ipaddr_addr(config.wifi_netmask);
        ip_info.gw.addr = ipaddr_addr(config.wifi_gw);
        
        esp_netif_set_ip_info(s_wifi_netif, &ip_info);
    }

    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, config.wifi_ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, config.wifi_pass, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_current_mode = WIFI_MODE_STA;
    return ESP_OK;
}

esp_err_t wifi_manager_stop(void)
{
    if (s_current_mode == WIFI_MODE_NULL) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Parando Wi-Fi...");
    esp_wifi_stop();
    
    if (s_wifi_netif) {
        esp_netif_destroy(s_wifi_netif);
        s_wifi_netif = NULL;
    }

    s_current_mode = WIFI_MODE_NULL;
    s_connected = false;
    return ESP_OK;
}

bool wifi_manager_is_connected(void)
{
    return s_connected;
}

esp_netif_t* wifi_manager_get_netif(void)
{
    return s_wifi_netif;
}
