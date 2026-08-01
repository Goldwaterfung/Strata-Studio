# Layer4: sends
cmake_minimum_required(VERSION 3.20)

add_library(layer4_sends STATIC send_node.cpp)

target_include_directories(layer4_sends PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer4_sends PUBLIC common_utils)
