# Monitor Switch DSP Node
add_library(dsp_monitor_switch STATIC monitor_switch_node.cpp)

target_link_libraries(dsp_monitor_switch PUBLIC
    common_dsp
)
