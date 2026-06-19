idf_component_register(SRCS "web_config.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES "esp_http_server" "storage_manager" "esp_event" "esp_netif" "json"
                       EMBED_TXTFILES "index.html")
