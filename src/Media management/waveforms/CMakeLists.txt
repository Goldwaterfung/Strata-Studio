# Layer 6: Waveform Cache
cmake_minimum_required(VERSION 3.20)

add_library(layer6_waveforms STATIC
    waveform_primitives.h
    iwaveform_renderer.h
    waveform_renderer_impl.h
    waveform_renderer_impl.cpp
)

target_include_directories(layer6_waveforms PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(layer6_waveforms PUBLIC 
    common_utils
    layer2
    layer6_registry
    layer6_codecs
)
