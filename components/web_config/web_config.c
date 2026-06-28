#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "storage_manager.h"
#include "network_manager.h"
#include "dns_server.h"
#include "ethernet_w5500.h"
#include "wifi_manager.h"
#include "web_config.h"

static const char *TAG = "WebConfig";
static httpd_handle_t s_server = NULL;

// Referencias ao arquivo index.html embarcado
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

// Handler para servir a pagina inicial index.html
static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    
    // O calculo da diferenca nos da o tamanho exato do arquivo embarcado
    size_t index_html_len = index_html_end - index_html_start;
    
    return httpd_resp_send(req, (const char *)index_html_start, index_html_len);
}

// Handler para fornecer os dados de configuracao atuais em JSON
static esp_err_t config_api_get_handler(httpd_req_t *req)
{
    faroldns_config_t config;
    if (storage_load_config(&config) != ESP_OK) {
        storage_get_default_config(&config);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "net_mode", config.net_mode);
    cJSON_AddStringToObject(root, "hostname", config.hostname);
    cJSON_AddStringToObject(root, "upstream_dns", config.upstream_dns);
    
    cJSON_AddBoolToObject(root, "dhcp_enabled", config.dhcp_enabled);
    cJSON_AddStringToObject(root, "ip", config.ip);
    cJSON_AddStringToObject(root, "netmask", config.netmask);
    cJSON_AddStringToObject(root, "gw", config.gw);
    
    cJSON_AddStringToObject(root, "wifi_ssid", config.wifi_ssid);
    cJSON_AddStringToObject(root, "wifi_pass", config.wifi_pass);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free(json_str);

    return ESP_OK;
}

// Handler para fornecer status do sistema em JSON
static esp_err_t status_api_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    network_state_t state = network_manager_get_state();
    cJSON_AddStringToObject(root, "state", network_manager_state_to_str(state));

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_ETH);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(root, "mac", mac_str);

    // Tenta obter IP da interface ativa
    esp_netif_t *active_netif = NULL;
    if (state == NET_STATE_ETHERNET) {
        active_netif = eth_w5500_get_netif();
    } else if (state == NET_STATE_WIFI_STA || state == NET_STATE_WIFI_AP) {
        active_netif = wifi_manager_get_netif();
    }

    if (active_netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(active_netif, &ip_info) == ESP_OK) {
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
            cJSON_AddStringToObject(root, "ip", ip_str);
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.netmask));
            cJSON_AddStringToObject(root, "netmask", ip_str);
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.gw));
            cJSON_AddStringToObject(root, "gw", ip_str);
        }
    }

    // Uptime em segundos
    int64_t uptime_us = esp_timer_get_time();
    cJSON_AddNumberToObject(root, "uptime_sec", (int32_t)(uptime_us / 1000000));

    // Estatisticas do DNS
    cJSON_AddNumberToObject(root, "dns_queries", dns_server_get_query_count());
    cJSON_AddNumberToObject(root, "bytes_sent", dns_server_get_bytes_sent());
    cJSON_AddNumberToObject(root, "bytes_received", dns_server_get_bytes_received());

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free(json_str);

    return ESP_OK;
}

// Tarefa para reiniciar o ESP32 com atraso
static void restart_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "Reiniciando o sistema...");
    esp_restart();
    vTaskDelete(NULL);
}

// Handler para salvar as configuracoes recebidas via POST JSON
static esp_err_t config_save_post_handler(httpd_req_t *req)
{
    char buf[1024];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Tamanho do payload JSON excede o buffer.");
        return ESP_FAIL;
    }

    int bytes_read = 0;
    while (remaining > 0) {
        if ((ret = httpd_req_recv(req, buf + bytes_read, remaining)) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        bytes_read += ret;
        remaining -= ret;
    }
    buf[bytes_read] = '\0';

    ESP_LOGI(TAG, "Configuracao recebida via POST: %s", buf);

    // Carrega configuracao atual
    faroldns_config_t config;
    if (storage_load_config(&config) != ESP_OK) {
        storage_get_default_config(&config);
    }

    // Realiza o parse do JSON
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalido.");
        return ESP_FAIL;
    }

    cJSON *item = cJSON_GetObjectItem(root, "hostname");
    if (cJSON_IsString(item)) {
        strncpy(config.hostname, item->valuestring, sizeof(config.hostname) - 1);
    }

    item = cJSON_GetObjectItem(root, "upstream_dns");
    if (cJSON_IsString(item)) {
        strncpy(config.upstream_dns, item->valuestring, sizeof(config.upstream_dns) - 1);
    }

    item = cJSON_GetObjectItem(root, "dhcp_enabled");
    if (cJSON_IsBool(item)) {
        config.dhcp_enabled = cJSON_IsTrue(item);
    }

    item = cJSON_GetObjectItem(root, "ip");
    if (cJSON_IsString(item)) {
        strncpy(config.ip, item->valuestring, sizeof(config.ip) - 1);
    }

    item = cJSON_GetObjectItem(root, "netmask");
    if (cJSON_IsString(item)) {
        strncpy(config.netmask, item->valuestring, sizeof(config.netmask) - 1);
    }

    item = cJSON_GetObjectItem(root, "gw");
    if (cJSON_IsString(item)) {
        strncpy(config.gw, item->valuestring, sizeof(config.gw) - 1);
    }

    item = cJSON_GetObjectItem(root, "wifi_ssid");
    if (cJSON_IsString(item)) {
        strncpy(config.wifi_ssid, item->valuestring, sizeof(config.wifi_ssid) - 1);
    }

    item = cJSON_GetObjectItem(root, "wifi_pass");
    if (cJSON_IsString(item)) {
        strncpy(config.wifi_pass, item->valuestring, sizeof(config.wifi_pass) - 1);
    }

    item = cJSON_GetObjectItem(root, "net_mode");
    if (cJSON_IsNumber(item)) {
        int mode = item->valueint;
        if (mode >= NET_MODE_AUTO && mode <= NET_MODE_WIFI_ONLY) {
            config.net_mode = (uint8_t)mode;
        }
    }

    cJSON_Delete(root);

    // Salva na NVS
    esp_err_t err = storage_save_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao gravar config no NVS: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falha ao gravar na Flash.");
        return ESP_FAIL;
    }

    // Envia resposta de sucesso e depois reinicia
    httpd_resp_sendstr(req, "OK");
    
    // Dispara reinicio agendado
    xTaskCreate(restart_task, "restart_task", 2048, NULL, 5, NULL);

    return ESP_OK;
}

// Configuracoes das rotas do servidor web
static const httpd_uri_t index_get_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t config_get_uri = {
    .uri       = "/api/config",
    .method    = HTTP_GET,
    .handler   = config_api_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t config_save_post_uri = {
    .uri       = "/save",
    .method    = HTTP_POST,
    .handler   = config_save_post_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t status_get_uri = {
    .uri       = "/api/status",
    .method    = HTTP_GET,
    .handler   = status_api_get_handler,
    .user_ctx  = NULL
};

esp_err_t web_config_start(void)
{
    if (s_server != NULL) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Iniciando servidor HTTP de configuracao na porta 80...");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192; // Aumentado para lidar com cJSON e sockets confortavelmente

    esp_err_t err = httpd_start(&s_server, &config);
    if (err == ESP_OK) {
        httpd_register_uri_handler(s_server, &index_get_uri);
        httpd_register_uri_handler(s_server, &config_get_uri);
        httpd_register_uri_handler(s_server, &config_save_post_uri);
        httpd_register_uri_handler(s_server, &status_get_uri);
        ESP_LOGI(TAG, "Servidor HTTP iniciado com sucesso.");
    } else {
        ESP_LOGE(TAG, "Falha ao iniciar servidor HTTP: %s", esp_err_to_name(err));
    }

    return err;
}

void web_config_stop(void)
{
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "Servidor HTTP de configuracao parado.");
    }
}
