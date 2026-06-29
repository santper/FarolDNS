#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_manager.h"
#include "storage_manager.h"
#include "mdns_manager.h"

static const char *TAG = "mDNSManager";
static bool s_probe_done = false;
static char s_hostname[32] = "";

static void mdns_probe_task(void *pvParameters)
{
    // Aguarda a rede ficar pronta antes de sondar
    while (network_manager_get_state() == NET_STATE_INIT) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    faroldns_config_t config;
    if (storage_load_config(&config) != ESP_OK) {
        storage_get_default_config(&config);
    }

    // Sonda sequencial: FarolDNS1, FarolDNS2, ...
    char hostname[32];
    int counter = 1;
    bool found = false;

    while (counter <= 10 && !found) {
        snprintf(hostname, sizeof(hostname), "faroldns%d", counter);
        ESP_LOGI(TAG, "Sondando hostname: %s.local", hostname);

        // Consulta primeiro (sem setar hostname ainda)
        esp_ip4_addr_t addr;
        esp_err_t err = mdns_query_a(hostname, 1000, &addr);

        if (err == ESP_OK) {
            ESP_LOGW(TAG, "Hostname %s ja em uso. Tentando proximo...",
                     hostname);
            counter++;
        } else {
            ESP_LOGI(TAG, "Hostname %s disponivel!", hostname);
            found = true;
        }
    }

    if (!found) {
        ESP_LOGW(TAG, "Nao foi possivel encontrar hostname unico. Usando: %s", hostname);
    }

    strncpy(s_hostname, hostname, sizeof(s_hostname) - 1);
    esp_err_t err = mdns_hostname_set(s_hostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao definir hostname final: %s", esp_err_to_name(err));
    }

    // Atualiza o hostname na config salva para persistir a escolha
    strncpy(config.hostname, s_hostname, sizeof(config.hostname) - 1);
    storage_save_config(&config);

    // Seta nome amigavel e registra servico HTTP (agora com hostname definido)
    esp_err_t err2 = mdns_instance_name_set("Servidor FarolDNS");
    if (err2 != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao definir nome da instancia: %s", esp_err_to_name(err2));
    }
    err2 = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    if (err2 != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao registrar servico HTTP: %s", esp_err_to_name(err2));
    } else {
        ESP_LOGI(TAG, "Servico HTTP registrado no mDNS");
    }

    s_probe_done = true;
    vTaskDelete(NULL);
}

esp_err_t mdns_manager_start(void)
{
    faroldns_config_t config;
    if (storage_load_config(&config) != ESP_OK) {
        storage_get_default_config(&config);
    }

    ESP_LOGI(TAG, "Iniciando mDNS...");

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar o servico mDNS: %s", esp_err_to_name(err));
        return err;
    }

    // Cria tarefa de sondagem para definir hostname unico apos rede ativa
    BaseType_t ret = xTaskCreate(mdns_probe_task, "mdns_probe", 4096, NULL, 3, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Falha ao criar tarefa mdns_probe");
    }

    return ESP_OK;
}

bool mdns_manager_is_probe_done(void)
{
    return s_probe_done;
}

const char* mdns_manager_get_hostname(void)
{
    return s_hostname;
}

void mdns_manager_stop(void)
{
    mdns_free();
    s_probe_done = false;
    ESP_LOGI(TAG, "Servico mDNS finalizado.");
}