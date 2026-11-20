# Godwit dial, left side — Waveshare ESP32-S3-Knob-Touch-LCD-1.8 (ESP32-S3).
#
# All of the firmware lives in ../dial-common, shared with
# dial-waveshare-right and with the Elecrow dials. This project picks the
# board (the boards/ folder on EXTRA_COMPONENT_DIRS) and the side
# (sdkconfig.defaults).
#
#   idf.py set-target esp32s3     # first build on a fresh clone
#   idf.py build
#   ../dial-common/scripts/flash.sh waveshare-left
cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "0.1.0")

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../dial-common/components"
    "${CMAKE_CURRENT_LIST_DIR}/../dial-common/boards/waveshare-knob-1.8")
set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/../dial-common/sdkconfig.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/../dial-common/boards/waveshare-knob-1.8/sdkconfig.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(godwit_dial_waveshare_left)
