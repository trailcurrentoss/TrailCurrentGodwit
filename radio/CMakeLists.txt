cmake_minimum_required(VERSION 3.21)

project(godwit-radio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The filters are most of the work and vectorise well, once the compiler may
# reorder the sums in them
add_library(fmreceiver STATIC src/fmreceiver.cpp)
target_include_directories(fmreceiver PUBLIC src)
target_compile_options(fmreceiver PRIVATE -O3 -ffast-math)

add_library(adsb STATIC src/adsb.cpp)
target_include_directories(adsb PUBLIC src)
target_compile_options(adsb PRIVATE -O3)

add_library(ais STATIC src/ais.cpp)
target_include_directories(ais PUBLIC src)
target_compile_options(ais PRIVATE -O3)

add_library(wxradio STATIC src/wxradio.cpp)
target_include_directories(wxradio PUBLIC src)
target_link_libraries(wxradio PUBLIC fmreceiver)
target_compile_options(wxradio PRIVATE -O3 -ffast-math)

# The DSP's tests need no tuner, so they can run anywhere
enable_testing()
add_executable(fmreceiver-test tests/fmreceiver_test.cpp)
target_link_libraries(fmreceiver-test PRIVATE fmreceiver)
add_test(NAME fmreceiver COMMAND fmreceiver-test)
add_executable(adsb-test tests/adsb_test.cpp)
target_link_libraries(adsb-test PRIVATE adsb)
add_test(NAME adsb COMMAND adsb-test)
add_executable(ais-test tests/ais_test.cpp)
target_link_libraries(ais-test PRIVATE ais)
add_test(NAME ais COMMAND ais-test)
add_executable(wxradio-test tests/wxradio_test.cpp)
target_link_libraries(wxradio-test PRIVATE wxradio)
target_compile_definitions(wxradio-test PRIVATE WX_STATION_TABLE="${CMAKE_CURRENT_SOURCE_DIR}/data/nwr-stations.tsv")
add_test(NAME wxradio COMMAND wxradio-test)

# The tuner itself: only where librtlsdr is installed
find_library(RTLSDR_LIBRARY rtlsdr)
find_path(RTLSDR_INCLUDE_DIR rtl-sdr.h)
if(RTLSDR_LIBRARY AND RTLSDR_INCLUDE_DIR)
    find_package(Threads REQUIRED)
    add_executable(godwit-radio src/main.cpp)
    target_include_directories(godwit-radio PRIVATE ${RTLSDR_INCLUDE_DIR})
    target_link_libraries(godwit-radio PRIVATE fmreceiver ${RTLSDR_LIBRARY} Threads::Threads)
    add_executable(godwit-adsb src/adsb_main.cpp)
    target_include_directories(godwit-adsb PRIVATE ${RTLSDR_INCLUDE_DIR})
    target_link_libraries(godwit-adsb PRIVATE adsb ${RTLSDR_LIBRARY} Threads::Threads)
    add_executable(godwit-ais src/ais_main.cpp)
    target_include_directories(godwit-ais PRIVATE ${RTLSDR_INCLUDE_DIR})
    target_link_libraries(godwit-ais PRIVATE ais ${RTLSDR_LIBRARY} Threads::Threads)
    add_executable(godwit-wxradio src/wxradio_main.cpp)
    target_include_directories(godwit-wxradio PRIVATE ${RTLSDR_INCLUDE_DIR})
    target_link_libraries(godwit-wxradio PRIVATE wxradio ${RTLSDR_LIBRARY} Threads::Threads)
    include(GNUInstallDirs)
    install(TARGETS godwit-radio godwit-adsb godwit-ais godwit-wxradio RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    # The transmitters, so the app can name them with no internet
    install(FILES data/nwr-stations.tsv DESTINATION ${CMAKE_INSTALL_DATADIR}/godwit/weather)
else()
    message(STATUS "librtlsdr not found: building the DSP and its tests only")
endif()
