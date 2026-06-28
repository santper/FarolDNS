#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief Inicializa e inicia o servidor DNS modular.
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t dns_server_start(void);

/**
 * @brief Para o servidor DNS.
 */
void dns_server_stop(void);

/**
 * @brief Obtém o total de consultas DNS respondidas.
 */
uint32_t dns_server_get_query_count(void);

/**
 * @brief Obtém o total de bytes enviados (respostas aos clientes).
 */
uint32_t dns_server_get_bytes_sent(void);

/**
 * @brief Obtém o total de bytes recebidos (consultas dos clientes).
 */
uint32_t dns_server_get_bytes_received(void);

#endif // DNS_SERVER_H
