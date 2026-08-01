# Layer 5: track_manager
cmake_minimum_required(VERSION 3.20)

add_library(layer5_track_manager STATIC 
    itrack_manager.h
    track_manager_impl.h
    track_manager_impl.cpp
    track_commands.h
)

target_include_directories(layer5_track_manager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer5_track_manager PUBLIC 
    common_utils
    layer5_interfaces
    layer5_playlist
    layer5_midi_sequencer
    layer5_command_history
)
