# Layer 5: region_manager
cmake_minimum_required(VERSION 3.20)

add_library(layer5_region_manager STATIC 
    iaudio_region_source_manager.h
    region_source_manager_impl.h
    region_source_manager_impl.cpp
    source_manager_commands.h
)

target_include_directories(layer5_region_manager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer5_region_manager PUBLIC 
    common_utils
)
