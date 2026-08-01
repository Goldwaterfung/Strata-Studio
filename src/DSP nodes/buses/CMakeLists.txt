# Layer4: buses
cmake_minimum_required(VERSION 3.20)

add_library(dsp_buses STATIC bus_node.cpp)

target_link_libraries(dsp_buses PUBLIC
    common_dsp
    common_math
)
