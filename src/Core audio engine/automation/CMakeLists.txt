# Layer3: automation
cmake_minimum_required(VERSION 3.20)

add_library(layer3_automation INTERFACE)

target_include_directories(layer3_automation INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/Core\ infrastructure
)

target_link_libraries(layer3_automation INTERFACE
    common_utils
    layer2_bridges
)

target_compile_features(layer3_automation INTERFACE cxx_std_20)

# Source files for automation processor
set(AUTOMATION_SOURCES
    automation_processor_impl.cpp
)

# Create implementation library
add_library(layer3_automation_impl STATIC ${AUTOMATION_SOURCES})
target_link_libraries(layer3_automation_impl PUBLIC
    layer3_automation
    layer2_bridges
)
target_include_directories(layer3_automation_impl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/Core\ infrastructure
)
