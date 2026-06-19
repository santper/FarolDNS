idf_component_register(SRCS "network_manager.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES "esp_event" "esp_netif" "esp_eth" "esp_wifi" "storage_manager" "ethernet_w5500" "wifi_manager")
