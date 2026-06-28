idf_component_register(SRCS "web_config.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES "esp_http_server" "storage_manager" "network_manager" "dns_server" "ethernet_w5500" "wifi_manager" "esp_event" "esp_netif" "esp_timer" "json"
                       EMBED_TXTFILES "index.html")
