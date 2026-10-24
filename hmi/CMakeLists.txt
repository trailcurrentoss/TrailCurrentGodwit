cmake_minimum_required(VERSION 3.21)

project(godwit-hmi VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick Svg DBus Network)
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
        qml/HeadUnit.qml
        qml/Simulator.qml
        qml/components/Icon.qml
        qml/components/Card.qml
        qml/components/Pill.qml
        qml/components/Num.qml
        qml/components/ListRows.qml
        qml/components/Switch.qml
        qml/components/Keyboard.qml
        qml/components/AppPage.qml
        qml/chrome/StatusBar.qml
        qml/chrome/BottomBar.qml
        qml/chrome/Launcher.qml
        qml/chrome/IncomingBanner.qml
        qml/chrome/MessageBanner.qml
        qml/chrome/KnobPanel.qml
        qml/chrome/VolumeHud.qml
        qml/chrome/SafetyNotice.qml
        qml/knob/KnobFace.qml
        qml/knob/KnobSim.qml
        qml/apps/NavApp.qml
        qml/apps/NavMap.qml
        qml/apps/MediaApp.qml
        qml/apps/RadioApp.qml
        qml/apps/ClimateApp.qml
        qml/apps/PhoneApp.qml
        qml/apps/CallView.qml
        qml/apps/EnergyApp.qml
        qml/apps/WaterApp.qml
        qml/apps/LevelApp.qml
        qml/apps/LightsApp.qml
        qml/apps/CamsApp.qml
        qml/apps/SettingsApp.qml
    RESOURCES
        assets/fonts/Inter-Regular.ttf
        assets/fonts/Inter-SemiBold.ttf
        assets/fonts/Inter-Bold.ttf
        assets/shadow.png
        assets/shadow-lg.png
        assets/vehicle/van_top_light.png
        assets/vehicle/van_top_dark.png
        assets/water/water_tank_light.png
        assets/water/water_tank_dark.png
        assets/fx/glow_heat.png
        assets/fx/glow_cool.png
        assets/fx/glow_air.png
        assets/fx/streak_heat.png
        assets/fx/streak_cool.png
        assets/fx/streak_air.png
)

file(GLOB GODWIT_ICONS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} assets/icons/*.svg)
qt_add_resources(godwit-hmi icons PREFIX / FILES ${GODWIT_ICONS})
file(GLOB GODWIT_LICENSES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} assets/licenses/*.txt)
qt_add_resources(godwit-hmi licenses PREFIX / FILES ${GODWIT_LICENSES})

target_include_directories(godwit-hmi PRIVATE src)
target_link_libraries(godwit-hmi PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Svg Qt6::DBus Qt6::Network)

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

include(GNUInstallDirs)
install(TARGETS godwit-hmi RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
