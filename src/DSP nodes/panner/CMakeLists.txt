# Layer4: panner
cmake_minimum_required(VERSION 3.20)

add_library(layer4_panner STATIC 
    panner_node.h
    panner_node.cpp
)

target_include_directories(layer4_panner PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer4_panner PUBLIC common_utils)
