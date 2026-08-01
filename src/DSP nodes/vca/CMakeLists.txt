# Layer4: vca
cmake_minimum_required(VERSION 3.20)

add_library(layer4_vca STATIC 
    vca_node.h
    vca_node.cpp
)

target_include_directories(layer4_vca PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer4_vca PUBLIC common_utils)
