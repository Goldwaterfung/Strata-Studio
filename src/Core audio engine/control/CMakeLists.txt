# Layer3: control
cmake_minimum_required(VERSION 3.20)

add_library(layer3_control INTERFACE)

target_include_directories(layer3_control INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(layer3_control INTERFACE common_utils)

# Control surface manager implementation
add_library(layer3_control_surface_impl STATIC
    control_surface_manager_impl.cpp
)

target_link_libraries(layer3_control_surface_impl PRIVATE
    layer3_control
    layer2_bridges
    common_utils
)

target_include_directories(layer3_control_surface_impl PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
