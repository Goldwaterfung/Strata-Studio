# src/Core audio engine/engine/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

add_library(layer3_engine STATIC
    audio_engine_impl.cpp
    audio_engine_impl.h
    iaudio_engine.h
)

target_link_libraries(layer3_engine PRIVATE
    layer3_interface
    layer2
    layer1_interface
)

target_include_directories(layer3_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
)
