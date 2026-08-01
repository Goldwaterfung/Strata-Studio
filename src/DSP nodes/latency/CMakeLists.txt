# Latency DSP Node (PDC Testing)
add_library(dsp_latency STATIC latency_node.cpp)

target_link_libraries(dsp_latency PUBLIC
    common_dsp
)
