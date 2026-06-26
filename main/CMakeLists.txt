idf_component_register(
    SRCS
        "main.c"
        "wifi.c"
        "mc_protocol.c"
        "varint.c"
        "nbt.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_timer
    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        lwip
)
