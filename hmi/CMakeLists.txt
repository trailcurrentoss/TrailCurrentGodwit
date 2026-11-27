cmake_minimum_required(VERSION 3.21)

project(godwit-hmi VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick Svg DBus Network Multimedia)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(godwit-hmi
    src/main.cpp
    src/iconprovider.cpp src/iconprovider.h
    src/knobs.cpp src/knobs.h
    src/dials.cpp src/dials.h src/dialprotocol.h
    src/config.h
    src/connman.cpp src/connman.h
    src/bluez.cpp src/bluez.h
    src/camera.cpp src/camera.h
    src/ofono.cpp src/ofono.h
    src/phonebook.cpp src/phonebook.h
    src/mixer.cpp src/mixer.h
    src/places.cpp src/places.h
    src/router.cpp src/router.h
    src/clock.cpp src/clock.h
    src/gnss.cpp src/gnss.h
    src/voice.cpp src/voice.h
    src/messages.cpp src/messages.h
    src/dictation.cpp src/dictation.h
    src/library.cpp src/library.h
    src/trails.cpp src/trails.h
    src/wake.cpp src/wake.h
    src/logs.cpp src/logs.h
    src/power.cpp src/power.h
    src/canbus.cpp src/canbus.h
    src/dbc.cpp src/dbc.h
    src/trailcurrent.cpp src/trailcurrent.h
    src/solar.cpp src/solar.h
    src/tracks.cpp src/tracks.h
    src/timing.cpp src/timing.h
    src/radio.cpp src/radio.h
    src/tunerreceiver.cpp src/tunerreceiver.h
    src/aircraft.cpp src/aircraft.h
    src/ships.cpp src/ships.h
    src/weatherradio.cpp src/weatherradio.h
    src/llm.cpp src/llm.h
    src/music.cpp src/music.h
)

set_source_files_properties(qml/Theme.qml qml/Hmi.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(godwit-hmi
    URI Godwit
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Hmi.qml
        qml/Ring.js
        qml/DemoData.js
        qml/Legal.js
        qml/Intents.js
        qml/FaIcons.js
        qml/HeadUnit.qml
        qml/Simulator.qml
        qml/components/Icon.qml
        qml/components/FaIcon.qml
        qml/components/Card.qml
        qml/components/EnergySky.qml
        qml/components/TrackDrawing.qml
        qml/components/TrackPicker.qml
        qml/components/SpeedTrace.qml
        qml/components/Pill.qml
        qml/components/Num.qml
        qml/components/ListRows.qml
        qml/components/Switch.qml
        qml/components/Keyboard.qml
        qml/components/AppPage.qml
        qml/components/TrafficPage.qml
        qml/components/UsbImport.qml
        qml/components/ConfirmSheet.qml
        qml/chrome/TrailNameSheet.qml
        qml/chrome/StatusBar.qml
        qml/chrome/BottomBar.qml
        qml/chrome/Launcher.qml
        qml/chrome/IncomingBanner.qml
        qml/chrome/MessageBanner.qml
        qml/chrome/AssistantBanner.qml
        qml/chrome/KnobPanel.qml
        qml/chrome/VolumeHud.qml
        qml/chrome/SafetyNotice.qml
        qml/knob/KnobFace.qml
        qml/knob/KnobSim.qml
        qml/apps/NavApp.qml
        qml/apps/NavMap.qml
        qml/apps/TrailsApp.qml
        qml/apps/MediaApp.qml
        qml/apps/RadioApp.qml
        qml/apps/AircraftApp.qml
        qml/apps/ShipsApp.qml
        qml/apps/WeatherApp.qml
        qml/apps/ClimateApp.qml
        qml/apps/PhoneApp.qml
        qml/apps/CallView.qml
        qml/apps/EnergyApp.qml
        qml/apps/WaterApp.qml
        qml/apps/LevelApp.qml
        qml/apps/TrackApp.qml
        qml/apps/LightsApp.qml
        qml/apps/CamsApp.qml
        qml/apps/PicturesApp.qml
        qml/apps/VideosApp.qml
        qml/apps/SettingsApp.qml
        qml/apps/LightButtonsSetup.qml
    RESOURCES
        assets/fonts/Inter-Regular.ttf
        assets/fonts/Inter-SemiBold.ttf
        assets/fonts/Inter-Bold.ttf
        assets/fonts/fa-solid-900.ttf
        assets/shadow.png
        assets/shadow-lg.png
        assets/vehicle/van_top_light.png
        assets/vehicle/van_top_dark.png
        assets/track/trophy_light.png
        assets/track/trophy_dark.png
        assets/track/tree_off_light.png
        assets/track/tree_on_light.png
        assets/track/tree_off_dark.png
        assets/track/tree_on_dark.png
        assets/track/glow_amber.png
        assets/track/glow_green.png
        assets/track/glow_red.png
        assets/track/glow_stage.png
        assets/water/water_tank_light.png
        assets/water/water_tank_dark.png
        assets/fx/glow_heat.png
        assets/fx/glow_cool.png
        assets/fx/glow_air.png
        assets/fx/streak_heat.png
        assets/fx/streak_cool.png
        assets/fx/streak_air.png
        assets/energy/battery_dark.png
        assets/energy/battery_light.png
        assets/energy/cloud1_dark.png
        assets/energy/cloud1_light.png
        assets/energy/cloud2_dark.png
        assets/energy/cloud2_light.png
        assets/energy/cloud3_dark.png
        assets/energy/cloud3_light.png
        assets/energy/land_day_dark.png
        assets/energy/land_day_light.png
        assets/energy/land_dusk_dark.png
        assets/energy/land_dusk_light.png
        assets/energy/land_low_dark.png
        assets/energy/land_low_light.png
        assets/energy/land_night_dark.png
        assets/energy/land_night_light.png
        assets/energy/land_overcast_dark.png
        assets/energy/land_overcast_light.png
        assets/energy/moon_0_dark.png
        assets/energy/moon_0_light.png
        assets/energy/moon_1_dark.png
        assets/energy/moon_1_light.png
        assets/energy/moon_2_dark.png
        assets/energy/moon_2_light.png
        assets/energy/moon_3_dark.png
        assets/energy/moon_3_light.png
        assets/energy/moon_4_dark.png
        assets/energy/moon_4_light.png
        assets/energy/moon_5_dark.png
        assets/energy/moon_5_light.png
        assets/energy/moon_6_dark.png
        assets/energy/moon_6_light.png
        assets/energy/moon_7_dark.png
        assets/energy/moon_7_light.png
        assets/energy/sky_day_dark.jpg
        assets/energy/sky_day_light.jpg
        assets/energy/sky_dusk_dark.jpg
        assets/energy/sky_dusk_light.jpg
        assets/energy/sky_low_dark.jpg
        assets/energy/sky_low_light.jpg
        assets/energy/sky_night_dark.jpg
        assets/energy/sky_night_light.jpg
        assets/energy/sky_overcast_dark.jpg
        assets/energy/sky_overcast_light.jpg
        assets/energy/sun_disc.png
        assets/energy/sun_glow.png
        assets/energy/van_day_dark.png
        assets/energy/van_day_light.png
        assets/energy/van_dusk_dark.png
        assets/energy/van_dusk_light.png
        assets/energy/van_low_dark.png
        assets/energy/van_low_light.png
        assets/energy/van_night_dark.png
        assets/energy/van_night_light.png
        assets/energy/van_overcast_dark.png
        assets/energy/van_overcast_light.png
        assets/energy/van_windows.png
)

file(GLOB GODWIT_ICONS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} assets/icons/*.svg)
qt_add_resources(godwit-hmi icons PREFIX / FILES ${GODWIT_ICONS})
file(GLOB GODWIT_LICENSES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} assets/licenses/*.txt)
qt_add_resources(godwit-hmi licenses PREFIX / FILES ${GODWIT_LICENSES})

target_include_directories(godwit-hmi PRIVATE src)
target_link_libraries(godwit-hmi PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Svg Qt6::DBus Qt6::Network Qt6::Multimedia)

# Routing, where Valhalla is installed (the head unit image). Without it the
# HMI builds and runs with routing unavailable.
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(VALHALLA IMPORTED_TARGET libvalhalla liblz4)
endif()
if(VALHALLA_FOUND)
    target_compile_definitions(godwit-hmi PRIVATE GODWIT_HAVE_VALHALLA)
    target_link_libraries(godwit-hmi PRIVATE PkgConfig::VALHALLA)
endif()

# Music tags (title, artist, album, cover). Without TagLib the library's
# titles come from the file names.
if(PKG_CONFIG_FOUND)
    pkg_check_modules(TAGLIB IMPORTED_TARGET taglib)
endif()
if(TAGLIB_FOUND)
    target_compile_definitions(godwit-hmi PRIVATE GODWIT_HAVE_TAGLIB)
    target_link_libraries(godwit-hmi PRIVATE PkgConfig::TAGLIB)
endif()

# Unit tests, on the desktop: cmake -DGODWIT_TESTS=ON, then ctest
option(GODWIT_TESTS "Build the unit tests" OFF)
if(GODWIT_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(tst_dbc tests/tst_dbc.cpp src/dbc.cpp src/dbc.h)
    target_include_directories(tst_dbc PRIVATE src)
    target_compile_definitions(tst_dbc PRIVATE GODWIT_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(tst_dbc PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME dbc COMMAND tst_dbc)
    find_package(Qt6 REQUIRED COMPONENTS Qml)
    qt_add_executable(tst_trailcurrent tests/tst_trailcurrent.cpp tests/stub_music.cpp
        src/trailcurrent.cpp src/trailcurrent.h src/dbc.cpp src/dbc.h src/canbus.cpp src/canbus.h)
    target_include_directories(tst_trailcurrent PRIVATE src)
    target_link_libraries(tst_trailcurrent PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Multimedia Qt6::Test)
    add_test(NAME trailcurrent COMMAND tst_trailcurrent)
    qt_add_executable(tst_solar tests/tst_solar.cpp tests/stub_music.cpp src/solar.cpp src/solar.h)
    target_include_directories(tst_solar PRIVATE src)
    target_link_libraries(tst_solar PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Multimedia Qt6::Test)
    add_test(NAME solar COMMAND tst_solar)
endif()

include(GNUInstallDirs)
install(TARGETS godwit-hmi RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
