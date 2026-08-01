# Layer 5: arranger
cmake_minimum_required(VERSION 3.20)

add_library(layer5_arranger STATIC 
    iarranger_track.h
    arranger_track_impl.h
    arranger_track_impl.cpp
    arranger_commands.h
)

target_include_directories(layer5_arranger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer5_arranger PUBLIC 
    common_utils
    layer5_command_history
)
