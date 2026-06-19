#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define CONFIG_SSID_MAX_LEN 32
#define CONFIG_PASS_MAX_LEN 64
#define CONFIG_IP_MAX_LEN 16
#define CONFIG_HOSTNAME_MAX_LEN 32

typedef struct {
    char hostname[CONFIG_HOSTNAME_MAX_LEN];
    
    // Wi-Fi Station configuration
    char wifi_ssid[CONFIG_SSID_MAX_LEN];
    char wifi_pass[CONFIG_PASS_MAX_LEN];
    bool wifi_dhcp;
    char wifi_ip[CONFIG_IP_MAX_LEN];
    char wifi_netmask[CONFIG_IP_MAX_LEN];
    char wifi_gw[CONFIG_IP_MAX_LEN];
    
    // Ethernet configuration
    bool eth_dhcp;
    char eth_ip[CONFIG_IP_MAX_LEN];
    char eth_netmask[CONFIG_IP_MAX_LEN];
    char eth_gw[CONFIG_IP_MAX_LEN];
    
    // DNS configuration
    char upstream_dns[CONFIG_IP_MAX_LEN];
} faroldns_config_t;

/**
 * @brief Initialize NVS and storage manager.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t storage_init(void);

/**
 * @brief Load configuration from NVS.
 * @param config Pointer to the configuration struct to fill.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t storage_load_config(faroldns_config_t *config);

/**
 * @brief Save configuration to NVS.
 * @param config Pointer to the configuration struct to save.
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */
esp_err_t storage_save_config(const faroldns_config_t *config);

/**
 * @brief Reset configuration to default values.
 * @param config Pointer to the configuration struct to reset.
 */
void storage_get_default_config(faroldns_config_t *config);

#endif // STORAGE_MANAGER_H
