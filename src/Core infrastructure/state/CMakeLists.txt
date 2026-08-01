# src/Core infrastructure/state/CMakeLists.txt
# Layer 2: State Management

cmake_minimum_required(VERSION 3.20)

add_library(layer2_state INTERFACE)
add_library(${PROJECT_NAMESPACE}::layer2_state ALIAS layer2_state)

target_include_directories(layer2_state INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
)

target_link_libraries(layer2_state INTERFACE
    common_utils
    layer2_memory
)

# Source files
target_sources(layer2_state INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/istate_manager.h>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/state_manager.cpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/snapshot_pool.h>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/delta_store.h>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/undo_stack.h>
)

# Source files for compilation
set(STATE_SOURCES
    state_manager.cpp
)

target_sources(layer2_state PUBLIC ${STATE_SOURCES})

