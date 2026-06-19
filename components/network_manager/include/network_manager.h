#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "esp_err.h"

typedef enum {
    NET_STATE_INIT,
    NET_STATE_ETHERNET,
    NET_STATE_WIFI_STA,
    NET_STATE_WIFI_AP,
    NET_STATE_DISCONNECTED
} network_state_t;

/**
 * @brief Inicializa o gerenciador de rede e inicia a lógica de failover.
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t network_manager_start(void);

/**
 * @brief Obtém o estado atual da rede.
 * @return network_state_t estado atual.
 */
network_state_t network_manager_get_state(void);

/**
 * @brief Retorna uma representação em string do estado atual.
 * @param state O estado de rede.
 * @return const char* nome do estado.
 */
const char* network_manager_state_to_str(network_state_t state);

#endif // NETWORK_MANAGER_H
