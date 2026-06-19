#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "mdns.h"
#include "storage_manager.h"
#include "mdns_manager.h"

static const char *TAG = "mDNSManager";

esp_err_t mdns_manager_start(void)
{
    // Carrega o hostname configurado (default: FarolDNS)
    faroldns_config_t config;
    if (storage_load_config(&config) != ESP_OK) {
        storage_get_default_config(&config);
    }

    ESP_LOGI(TAG, "Iniciando mDNS para o hostname: %s.local", config.hostname);

    // Inicializa o servico mDNS
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar o servico mDNS: %s", esp_err_to_name(err));
        return err;
    }

    // Configura o hostname no barramento local
    err = mdns_hostname_set(config.hostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao definir o hostname do mDNS: %s", esp_err_to_name(err));
        return err;
    }

    // Configura o nome de exibicao amigavel para discovery
    err = mdns_instance_name_set("Servidor FarolDNS");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao definir o nome da instancia do mDNS: %s", esp_err_to_name(err));
    }

    // Adiciona o servico HTTP na porta 80 (para o painel de configuracao)
    err = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao registrar servico HTTP no mDNS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Servico HTTP registrado no mDNS com sucesso.");
    }

    return ESP_OK;
}

void mdns_manager_stop(void)
{
    mdns_free();
    ESP_LOGI(TAG, "Servico mDNS finalizado.");
}
