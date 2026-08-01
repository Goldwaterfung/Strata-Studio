# Layer 5: midi_sequencer
cmake_minimum_required(VERSION 3.20)

add_library(layer5_midi_sequencer STATIC 
    imidi_sequencer.h
    midi_sequencer_impl.h
    midi_sequencer_impl.cpp
    quantization.h
)

target_include_directories(layer5_midi_sequencer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer5_midi_sequencer PUBLIC 
    common_utils
)
