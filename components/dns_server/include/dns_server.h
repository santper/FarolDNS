#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#include "esp_err.h"

/**
 * @brief Inicializa e inicia o servidor DNS modular.
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t dns_server_start(void);

/**
 * @brief Para o servidor DNS.
 */
void dns_server_stop(void);

#endif // DNS_SERVER_H
