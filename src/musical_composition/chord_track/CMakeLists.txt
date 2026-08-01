# Layer 5: chord_track
cmake_minimum_required(VERSION 3.20)

add_library(layer5_chord_track STATIC 
    ichord_track.h
    chord_track_impl.h
    chord_track_impl.cpp
    chord_commands.h
)

target_include_directories(layer5_chord_track PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer5_chord_track PUBLIC 
    common_utils
    layer5_command_history
)
