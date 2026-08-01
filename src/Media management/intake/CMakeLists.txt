# Layer 6: Media Intake Pipeline
cmake_minimum_required(VERSION 3.20)

add_library(layer6_intake STATIC
    imedia_intake_pipeline.h
    media_intake_pipeline_impl.cpp
)

target_include_directories(layer6_intake PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(layer6_intake PUBLIC 
    layer2
    layer6_registry
    layer6_codecs
    layer6_analysis
    layer6_waveforms
)
