# Layer4: freeze
cmake_minimum_required(VERSION 3.20)

add_library(layer4_freeze INTERFACE)

target_include_directories(layer4_freeze INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer4_freeze INTERFACE common_utils)
