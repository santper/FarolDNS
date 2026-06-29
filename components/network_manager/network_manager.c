#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_eth.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "ethernet_w5500.h"
#include "wifi_manager.h"
#include "storage_manager.h"
#include "network_manager.h"

static const char *TAG = "NetworkManager";
static network_state_t s_net_state = NET_STATE_INIT;
static SemaphoreHandle_t s_state_mutex = NULL;
static TaskHandle_t s_monitor_task_handle = NULL;

static void set_state(network_state_t new_state)
{
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_net_state = new_state;
    xSemaphoreGive(s_state_mutex);
    ESP_LOGI(TAG, "Estado de rede alterado para: %s", network_manager_state_to_str(new_state));
}

network_state_t network_manager_get_state(void)
{
    network_state_t state;
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    state = s_net_state;
    xSemaphoreGive(s_state_mutex);
    return state;
}

const char* network_manager_state_to_str(network_state_t state)
{
    switch (state) {
        case NET_STATE_INIT:         return "INICIALIZANDO";
        case NET_STATE_ETHERNET:     return "ETHERNET (ATIVO)";
        case NET_STATE_WIFI_STA:     return "WI-FI STATION (ATIVO)";
        case NET_STATE_WIFI_AP:      return "WI-FI ACCESS POINT (FALLBACK)";
        case NET_STATE_DISCONNECTED: return "DESCONECTADO";
        default:                     return "DESCONHECIDO";
    }
}

static void on_network_event(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    // Notifica a tarefa de monitoramento de rede para acordar imediatamente
    if (s_monitor_task_handle != NULL) {
        xTaskNotifyGive(s_monitor_task_handle);
    }
}

static void network_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Tarefa de monitoramento de rede iniciada.");

    faroldns_config_t config;
    if (storage_load_config(&config) != ESP_OK) {
        storage_get_default_config(&config);
    }

    // Só aguarda link Ethernet se o modo permitir uso da interface cabeada
    if (config.net_mode != NET_MODE_WIFI_ONLY) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(8000));
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP_LOGI(TAG, "Boot phase complete. Initial Ethernet state: %sconnected",
                 eth_w5500_is_connected() ? "" : "not ");
    } else {
        ESP_LOGI(TAG, "Modo Wi-Fi only. Pulando espera de link Ethernet.");
    }

    while (1) {
        network_state_t current_state = network_manager_get_state();
        bool eth_ok = eth_w5500_is_connected() && (config.net_mode != NET_MODE_WIFI_ONLY);

        if (eth_ok) {
            if (current_state != NET_STATE_ETHERNET) {
                ESP_LOGI(TAG, "Ethernet conectada e ativa. Desativando Wi-Fi...");
                wifi_manager_stop();
                set_state(NET_STATE_ETHERNET);
            }
        } else {
            if (current_state == NET_STATE_ETHERNET || current_state == NET_STATE_INIT) {
                if (config.net_mode == NET_MODE_ETH_ONLY) {
                    ESP_LOGW(TAG, "Ethernet inativa. Modo ETH_ONLY: sem fallback Wi-Fi.");
                    set_state(NET_STATE_DISCONNECTED);
                } else {
                    ESP_LOGW(TAG, "Ethernet inativa ou modo Wi-Fi only. Iniciando Wi-Fi...");
                    if (strlen(config.wifi_ssid) > 0) {
                        set_state(NET_STATE_WIFI_STA);
                        wifi_manager_start_sta();
                    } else {
                        set_state(NET_STATE_WIFI_AP);
                        wifi_manager_start_ap();
                    }
                }
            }
        }

        // Dorme por ate 10 segundos, ou acorda instantaneamente se houver um evento de rede
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000));
    }
}

esp_err_t network_manager_start(void)
{
    s_state_mutex = xSemaphoreCreateMutex();
    if (s_state_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // Inicializa Ethernet W5500 (com a pinagem correta)
    esp_err_t err = eth_w5500_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar o hardware Ethernet W5500: %s", esp_err_to_name(err));
    }

    // Registra tratadores de eventos de rede para notificar a tarefa de monitoramento
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &on_network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &on_network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_network_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_network_event, NULL));

    // Cria a tarefa de monitoramento e orquestracao
    BaseType_t ret = xTaskCreate(network_monitor_task, "net_monitor", 4096, NULL, 4, &s_monitor_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Falha ao criar tarefa net_monitor");
        return ESP_FAIL;
    }

    return ESP_OK;
}
