# Sequencer DSP Nodes
add_library(dsp_sequencer STATIC
    audio_sequencer_node.cpp
    midi_sequencer_node.cpp
)

target_link_libraries(dsp_sequencer PUBLIC
    common_dsp
    layer3_streaming
)
