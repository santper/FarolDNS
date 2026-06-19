#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "storage_manager.h"

static const char *TAG = "StorageManager";
#define NVS_NAMESPACE "faroldns_cfg"
#define NVS_KEY_CONFIG "config"

esp_err_t storage_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS flash partition needs to be erased. Erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

void storage_get_default_config(faroldns_config_t *config)
{
    memset(config, 0, sizeof(faroldns_config_t));
    strncpy(config->hostname, "FarolDNS", sizeof(config->hostname) - 1);
    
    // Wi-Fi settings - default empty, DHCP enabled
    config->wifi_dhcp = true;
    
    // Ethernet settings - default DHCP enabled
    config->eth_dhcp = true;
    
    // DNS settings - default 1.1.1.1
    strncpy(config->upstream_dns, "1.1.1.1", sizeof(config->upstream_dns) - 1);
    
    ESP_LOGI(TAG, "Default configuration loaded");
}

esp_err_t storage_load_config(faroldns_config_t *config)
{
    nvs_handle_t my_handle;
    esp_err_t err;

    // First load defaults
    storage_get_default_config(config);

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "NVS namespace not found. Using defaults.");
            return ESP_OK; // Not an error, just means no config saved yet
        }
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    size_t required_size = sizeof(faroldns_config_t);
    err = nvs_get_blob(my_handle, NVS_KEY_CONFIG, config, &required_size);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "Config blob not found in NVS. Using defaults.");
            err = ESP_OK;
        } else {
            ESP_LOGE(TAG, "Error reading config blob: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGI(TAG, "Configuration successfully loaded from NVS");
    }

    nvs_close(my_handle);
    return err;
}

esp_err_t storage_save_config(const faroldns_config_t *config)
{
    nvs_handle_t my_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle for writing: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(my_handle, NVS_KEY_CONFIG, config, sizeof(faroldns_config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error writing config blob to NVS: %s", esp_err_to_name(err));
    } else {
        err = nvs_commit(my_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error committing config to NVS: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Configuration successfully saved to NVS");
        }
    }

    nvs_close(my_handle);
    return err;
}
