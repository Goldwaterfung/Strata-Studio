# Layer 5: interfaces
cmake_minimum_required(VERSION 3.20)

add_library(layer5_interfaces INTERFACE)

target_include_directories(layer5_interfaces INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer5_interfaces INTERFACE common_utils)
