set(src_dirs
    wifi_manager
    mqtt_app
    nvs_storage
)

set(include_dirs
    wifi_manager
    mqtt_app
    nvs_storage
)

set(requires
    nvs_flash
    esp_event
    esp_wifi
    esp_netif
    mqtt
)

idf_component_register(
    SRC_DIRS      ${src_dirs}
    INCLUDE_DIRS  ${include_dirs}
    REQUIRES      ${requires}
)
