#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define CONFIG_SSID_MAX_LEN 32
#define CONFIG_PASS_MAX_LEN 64
#define CONFIG_IP_MAX_LEN 16
#define CONFIG_HOSTNAME_MAX_LEN 32
#define CONFIG_VERSION_CURRENT 2

#define NET_MODE_AUTO      0
#define NET_MODE_ETH_ONLY  1
#define NET_MODE_WIFI_ONLY 2

typedef struct {
    uint8_t config_version;
    char hostname[CONFIG_HOSTNAME_MAX_LEN];
    
    // Wi-Fi Station configuration
    char wifi_ssid[CONFIG_SSID_MAX_LEN];
    char wifi_pass[CONFIG_PASS_MAX_LEN];
    
    // IP configuration (shared between Ethernet and Wi-Fi)
    bool dhcp_enabled;
    char ip[CONFIG_IP_MAX_LEN];
    char netmask[CONFIG_IP_MAX_LEN];
    char gw[CONFIG_IP_MAX_LEN];
    
    // DNS configuration
    char upstream_dns[CONFIG_IP_MAX_LEN];

    // Network mode selector
    uint8_t net_mode;
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
