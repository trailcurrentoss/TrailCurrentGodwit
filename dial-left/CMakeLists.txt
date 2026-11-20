# Godwit dial, left side — Elecrow CrowPanel 1.28" Rotary (ESP32-S3).
#
# All of the firmware lives in ../dial-common, shared with dial-right and
# with the Waveshare dials. This project picks the board (the boards/ folder
# on EXTRA_COMPONENT_DIRS) and the side (sdkconfig.defaults).
#
#   idf.py set-target esp32s3     # first build on a fresh clone
#   idf.py build
#   ../dial-common/scripts/flash.sh left
cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "0.1.2")

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../dial-common/components"
    "${CMAKE_CURRENT_LIST_DIR}/../dial-common/boards/crowpanel-1.28")
set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/../dial-common/sdkconfig.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/../dial-common/boards/crowpanel-1.28/sdkconfig.defaults"
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(godwit_dial_left)
