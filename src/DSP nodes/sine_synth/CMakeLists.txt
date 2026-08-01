# Sine Synth DSP Node
add_library(dsp_sine_synth STATIC sine_synth_node.cpp)

target_link_libraries(dsp_sine_synth PUBLIC
    common_dsp
)
