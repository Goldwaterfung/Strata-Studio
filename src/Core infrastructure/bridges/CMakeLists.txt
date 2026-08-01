# src/Core infrastructure/bridges/CMakeLists.txt
# Layer 2: RT/Main Thread Bridges

cmake_minimum_required(VERSION 3.20)

add_library(layer2_bridges INTERFACE)
add_library(${PROJECT_NAMESPACE}::layer2_bridges ALIAS layer2_bridges)

target_include_directories(layer2_bridges INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
)

target_link_libraries(layer2_bridges INTERFACE
    common_utils
    layer1_threading
    layer2_memory
)

# Source files
target_sources(layer2_bridges INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/imutation_bridge.h
    ${CMAKE_CURRENT_SOURCE_DIR}/mutation_bridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/spsc_queue.h
    ${CMAKE_CURRENT_SOURCE_DIR}/midi_spsc_ring_buffer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/itelemetry_bridge.h
    ${CMAKE_CURRENT_SOURCE_DIR}/telemetry_bridge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mpsc_queue.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ievent_queue.h
    ${CMAKE_CURRENT_SOURCE_DIR}/event_queue.cpp
)

