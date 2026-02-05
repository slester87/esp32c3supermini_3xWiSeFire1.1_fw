idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES led_strip esp_driver_gpio mdns esp_http_server esp_netif esp_wifi nvs_flash esp_timer spiffs)
