cmake_minimum_required(VERSION 3.21)

project(godwit-wake VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ONNX Runtime's C++ API: the onnxruntime-bin recipe on the head unit, or an
# unpacked release tarball (-DCMAKE_PREFIX_PATH=<onnxruntime-linux-*>) elsewhere
find_path(ORT_INCLUDE_DIR onnxruntime_cxx_api.h PATH_SUFFIXES onnxruntime include REQUIRED)
find_library(ORT_LIBRARY onnxruntime REQUIRED)

add_executable(godwit-wake src/main.cpp)
target_include_directories(godwit-wake PRIVATE ${ORT_INCLUDE_DIR})
target_link_libraries(godwit-wake PRIVATE ${ORT_LIBRARY})

include(GNUInstallDirs)
install(TARGETS godwit-wake RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES models/hey_peregrine.onnx models/hey_peregrine.onnx.data
        DESTINATION ${CMAKE_INSTALL_DATADIR}/godwit/wake)
