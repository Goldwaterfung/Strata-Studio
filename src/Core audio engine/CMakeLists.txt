# src/Core audio engine/CMakeLists.txt

cmake_minimum_required(VERSION 3.20)

#-------------------------------------------------------------------------------
# Layer 3 Interface Library (headers only)
#-------------------------------------------------------------------------------

add_library(layer3_interface INTERFACE)
target_include_directories(layer3_interface INTERFACE 
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/common"
    "${CMAKE_SOURCE_DIR}/src/Core infrastructure"
)
target_compile_features(layer3_interface INTERFACE cxx_std_20)

#-------------------------------------------------------------------------------
# Dependencies (from lower layers)
#-------------------------------------------------------------------------------

target_link_libraries(layer3_interface INTERFACE
    layer2_interface      # Layer 2 interfaces
    layer2                # Layer 2 implementations
    layer1_interface      # Layer 1 interfaces
    layer1                # Layer 1 implementations
)

#-------------------------------------------------------------------------------
# Add subsystems
#-------------------------------------------------------------------------------

add_subdirectory(scheduler)
add_subdirectory(transport)
add_subdirectory(plugin)
add_subdirectory(streaming)
add_subdirectory(automation)
add_subdirectory(control)
add_subdirectory(sidechain)
add_subdirectory(engine)

#-------------------------------------------------------------------------------
# Layer 3 aggregation library
#-------------------------------------------------------------------------------

add_library(layer3 STATIC)
target_link_libraries(layer3 PRIVATE
    layer3_scheduler
    layer3_transport
    layer3_plugin
    layer3_streaming
    layer3_automation_impl
    layer3_control_surface_impl
    layer3_sidechain_impl
    layer3_engine
)

# Export for Layer 4
target_include_directories(layer3 PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

#-------------------------------------------------------------------------------
# Compiler flags
#-------------------------------------------------------------------------------

if(NOT MSVC AND NOT APPLE)
    target_link_libraries(layer3 PRIVATE atomic)
endif()

#-------------------------------------------------------------------------------
# Platform-specific flags
#-------------------------------------------------------------------------------

if(APPLE)
    target_compile_options(layer3 PRIVATE 
        -mmacosx-version-min=10.15
    )
elseif(UNIX)
    target_compile_options(layer3 PRIVATE 
        -pthread
    )
elseif(MSVC)
    target_compile_options(layer3 PRIVATE 
        /MP
    )
endif()
