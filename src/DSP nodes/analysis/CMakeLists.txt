# Layer4: analysis
cmake_minimum_required(VERSION 3.20)

add_library(layer4_analysis STATIC 
    analysis_node.h
    analysis_node.cpp
)

target_include_directories(layer4_analysis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer4_analysis PUBLIC common_utils)
