#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include "storage_manager.h"
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
    cJSON_AddStringToObject(root, "hostname", config.hostname);
    cJSON_AddStringToObject(root, "upstream_dns", config.upstream_dns);
    
    cJSON_AddBoolToObject(root, "eth_dhcp", config.eth_dhcp);
    cJSON_AddStringToObject(root, "eth_ip", config.eth_ip);
    cJSON_AddStringToObject(root, "eth_netmask", config.eth_netmask);
    cJSON_AddStringToObject(root, "eth_gw", config.eth_gw);
    
    cJSON_AddStringToObject(root, "wifi_ssid", config.wifi_ssid);
    cJSON_AddStringToObject(root, "wifi_pass", config.wifi_pass);
    cJSON_AddBoolToObject(root, "wifi_dhcp", config.wifi_dhcp);
    cJSON_AddStringToObject(root, "wifi_ip", config.wifi_ip);
    cJSON_AddStringToObject(root, "wifi_netmask", config.wifi_netmask);
    cJSON_AddStringToObject(root, "wifi_gw", config.wifi_gw);

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

    item = cJSON_GetObjectItem(root, "eth_dhcp");
    if (cJSON_IsBool(item)) {
        config.eth_dhcp = cJSON_IsTrue(item);
    }

    item = cJSON_GetObjectItem(root, "eth_ip");
    if (cJSON_IsString(item)) {
        strncpy(config.eth_ip, item->valuestring, sizeof(config.eth_ip) - 1);
    }

    item = cJSON_GetObjectItem(root, "eth_netmask");
    if (cJSON_IsString(item)) {
        strncpy(config.eth_netmask, item->valuestring, sizeof(config.eth_netmask) - 1);
    }

    item = cJSON_GetObjectItem(root, "eth_gw");
    if (cJSON_IsString(item)) {
        strncpy(config.eth_gw, item->valuestring, sizeof(config.eth_gw) - 1);
    }

    item = cJSON_GetObjectItem(root, "wifi_ssid");
    if (cJSON_IsString(item)) {
        strncpy(config.wifi_ssid, item->valuestring, sizeof(config.wifi_ssid) - 1);
    }

    item = cJSON_GetObjectItem(root, "wifi_pass");
    if (cJSON_IsString(item)) {
        strncpy(config.wifi_pass, item->valuestring, sizeof(config.wifi_pass) - 1);
    }

    item = cJSON_GetObjectItem(root, "wifi_dhcp");
    if (cJSON_IsBool(item)) {
        config.wifi_dhcp = cJSON_IsTrue(item);
    }

    item = cJSON_GetObjectItem(root, "wifi_ip");
    if (cJSON_IsString(item)) {
        strncpy(config.wifi_ip, item->valuestring, sizeof(config.wifi_ip) - 1);
    }

    item = cJSON_GetObjectItem(root, "wifi_netmask");
    if (cJSON_IsString(item)) {
        strncpy(config.wifi_netmask, item->valuestring, sizeof(config.wifi_netmask) - 1);
    }

    item = cJSON_GetObjectItem(root, "wifi_gw");
    if (cJSON_IsString(item)) {
        strncpy(config.wifi_gw, item->valuestring, sizeof(config.wifi_gw) - 1);
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
