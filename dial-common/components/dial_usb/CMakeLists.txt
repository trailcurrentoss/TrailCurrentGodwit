idf_component_register(
    SRCS "src/dial_usb.c"
    INCLUDE_DIRS "include"
    REQUIRES espressif__esp_tinyusb
    PRIV_REQUIRES esp_system log
)
