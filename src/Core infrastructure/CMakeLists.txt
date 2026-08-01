# src/Core infrastructure/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

# Layer 2 Interface Library (headers only)
add_library(layer2_interface INTERFACE)
target_include_directories(layer2_interface INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
)
target_compile_features(layer2_interface INTERFACE cxx_std_20)

# Dependencies - Layer 1 components
target_link_libraries(layer2_interface INTERFACE
    layer1_interface
    layer1_threading
)

# Add subsystems
add_subdirectory(memory)
add_subdirectory(clock)
add_subdirectory(bridges)
add_subdirectory(state)
add_subdirectory(tempo)
add_subdirectory(plugins)

# Layer 2 aggregation (all implementations)
# Note: Layer 2 is currently INTERFACE-only until implementations are added
add_library(layer2 INTERFACE)
target_link_libraries(layer2 INTERFACE
    layer2_memory
    layer2_clock
    layer2_bridges
    layer2_state
    layer2_tempo
    layer2_plugins
)

# Export for Layer 3
target_include_directories(layer2 INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# Compiler flags for atomic operations
# if(NOT MSVC)
#     target_compile_options(layer2 PRIVATE -latomic)
# endif()

# Platform-specific flags
# if(UNIX AND NOT APPLE)
#     target_link_libraries(layer2 PRIVATE rt pthread)
# elseif(APPLE)
#     target_link_libraries(layer2 PRIVATE "-framework CoreFoundation")
# elseif(WIN32)
#     target_compile_definitions(layer2 PRIVATE WIN32_LEAN_AND_MEAN)
# endif()

