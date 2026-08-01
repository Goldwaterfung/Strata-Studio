# Layer 6: Preset Manager
cmake_minimum_required(VERSION 3.20)

add_library(layer6_presets STATIC
    ipreset_manager.h
    preset_manager_impl.cpp
)

target_include_directories(layer6_presets PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(layer6_presets PUBLIC 
    common_utils
)
