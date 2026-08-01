# Audio Input DSP Node
add_library(dsp_audio_input STATIC audio_input_node.cpp)

target_link_libraries(dsp_audio_input PUBLIC
    common_dsp
)
