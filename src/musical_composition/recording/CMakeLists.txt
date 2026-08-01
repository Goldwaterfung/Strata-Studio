# Layer 5: recording
cmake_minimum_required(VERSION 3.20)

add_library(layer5_recording STATIC 
    itake_recording_intake.h
    take_recording_intake.h
    take_recording_intake.cpp
)

target_include_directories(layer5_recording PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer5_recording PUBLIC 
    common_utils
    layer5_playlist
    layer5_region_manager
)
