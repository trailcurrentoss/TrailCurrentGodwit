# The dials draw their text in the head unit's own Inter fonts, so the two
# match. The TTFs live with the HMI and are embedded from there.
get_filename_component(GODWIT_FONTS "${CMAKE_CURRENT_LIST_DIR}/../../../hmi/assets/fonts" ABSOLUTE)

idf_component_register(
    SRCS
        "src/dial.c"
        "src/face.c"
        "src/icons.c"
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "src"
    REQUIRES
        crowpanel_board
        dial_usb
    PRIV_REQUIRES
        esp_app_format
        esp_timer
    EMBED_FILES
        "${GODWIT_FONTS}/Inter-Regular.ttf"
        "${GODWIT_FONTS}/Inter-SemiBold.ttf"
        "${GODWIT_FONTS}/Inter-Bold.ttf"
)
