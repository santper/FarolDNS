#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H

#include "esp_err.h"

/**
 * @brief Inicia o servidor HTTP para a interface web de configuração.
 * @return esp_err_t ESP_OK em caso de sucesso.
 */
esp_err_t web_config_start(void);

/**
 * @brief Para o servidor HTTP.
 */
void web_config_stop(void);

#endif // WEB_CONFIG_H
