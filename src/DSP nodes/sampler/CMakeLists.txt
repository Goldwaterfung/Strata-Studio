# Sampler DSP Node
add_library(dsp_sampler STATIC sampler_node.cpp)

target_link_libraries(dsp_sampler PUBLIC
    common_dsp
    layer3_streaming
)
