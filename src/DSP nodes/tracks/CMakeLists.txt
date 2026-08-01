# Layer4: tracks
cmake_minimum_required(VERSION 3.20)

add_library(layer4_tracks STATIC 
    audio_track_node.h
    audio_track_node.cpp
    instrument_track_node.h
    instrument_track_node.cpp
)

target_include_directories(layer4_tracks PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer4_tracks PUBLIC common_utils layer4_channelstrip layer4_plugins layer4_sends dsp_latency dsp_sine_synth)
