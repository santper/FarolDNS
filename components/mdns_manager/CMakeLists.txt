idf_component_register(SRCS "mdns_manager.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES "mdns" "storage_manager" "esp_event" "esp_netif")
