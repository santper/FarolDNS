#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_netif.h"

#define WIFI_AP_SSID "FarolDNS_Setup"

/**
 * @brief Inicializa o subsistema de Wi-Fi (configuração base).
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Inicia o Wi-Fi no modo Access Point (AP) para configuração.
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t wifi_manager_start_ap(void);

/**
 * @brief Inicia o Wi-Fi no modo Station (STA) para se conectar a um roteador.
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t wifi_manager_start_sta(void);

/**
 * @brief Para o Wi-Fi completamente para economizar energia.
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t wifi_manager_stop(void);

/**
 * @brief Verifica se o Wi-Fi Station está conectado com IP válido.
 * @return true se conectado, false caso contrário.
 */
bool wifi_manager_is_connected(void);

/**
 * @brief Obtém a interface netif do Wi-Fi (STA ou AP).
 * @return esp_netif_t* ponteiro para a netif.
 */
esp_netif_t* wifi_manager_get_netif(void);

#endif // WIFI_MANAGER_H
