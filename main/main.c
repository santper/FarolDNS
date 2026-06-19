#include <stdio.h>
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "storage_manager.h"
#include "network_manager.h"
#include "dns_server.h"
#include "mdns_manager.h"
#include "web_config.h"

static const char *TAG = "Main";

void app_main(void)
{
    ESP_LOGI(TAG, "[FarolDNS] Inicializando Servidor DNS Modular...");

    // 1. Inicializa a pilha TCP/IP e o loop de eventos padrao
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 2. Inicializa o modulo de armazenamento (NVS Flash)
    esp_err_t err = storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro fatal ao inicializar o armazenamento (NVS): %s", esp_err_to_name(err));
        return;
    }

    // 3. Inicializa o gerenciador de rede (Failover de Ethernet e Wi-Fi)
    err = network_manager_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao iniciar o gerenciador de rede: %s", esp_err_to_name(err));
    }

    // 4. Inicializa o servidor DNS Forwarder
    err = dns_server_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao iniciar o servidor DNS: %s", esp_err_to_name(err));
    }

    // 5. Inicializa o servico de Hostname Local (mDNS)
    err = mdns_manager_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao iniciar o servico mDNS: %s", esp_err_to_name(err));
    }

    // 6. Inicializa o painel de configuracoes via HTTP (Web Server)
    err = web_config_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao iniciar o servidor Web de configuracao: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "[FarolDNS] Sistema inicializado com sucesso. Rodando em FreeRTOS.");
}
