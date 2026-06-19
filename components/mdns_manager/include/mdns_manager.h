#ifndef MDNS_MANAGER_H
#define MDNS_MANAGER_H

#include "esp_err.h"

/**
 * @brief Inicializa e inicia o serviço mDNS no ESP32.
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t mdns_manager_start(void);

/**
 * @brief Para o serviço mDNS.
 */
void mdns_manager_stop(void);

#endif // MDNS_MANAGER_H
