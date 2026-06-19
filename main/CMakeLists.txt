idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES "storage_manager" "network_manager" "dns_server" "mdns_manager" "web_config" "esp_netif" "esp_event")
