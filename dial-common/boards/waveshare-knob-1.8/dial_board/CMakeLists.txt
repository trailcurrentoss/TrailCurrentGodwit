# The Waveshare ESP32-S3-Knob-Touch-LCD-1.8 as a Godwit dial board. The
# interface godwit_dial builds against is include/dial_board.h; the Elecrow
# board in ../../crowpanel-1.28 provides the same one.
idf_component_register(
    SRCS
        "src/dial_board.c"
        "src/board_display.c"
        "src/board_i2c.c"
        "src/board_touch.c"
        "src/board_haptic.c"
        "src/board_input.c"
        "src/board_ring.c"
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "src"
    REQUIRES
        esp_lcd
        espressif__esp_lvgl_port
        lvgl__lvgl
        esp_driver_gpio
        esp_driver_ledc
        esp_driver_spi
        esp_driver_i2c
        esp_timer
)
