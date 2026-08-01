# src/Core infrastructure/plugins/CMakeLists.txt
# Layer 2: Plugin Scanner

cmake_minimum_required(VERSION 3.20)

add_library(layer2_plugins INTERFACE)
add_library(${PROJECT_NAMESPACE}::layer2_plugins ALIAS layer2_plugins)

target_include_directories(layer2_plugins INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/third_party/VST3_SDK
)

target_link_libraries(layer2_plugins INTERFACE
    common_utils
    layer1_threading
    layer1_audio
    layer1_filesystem
)

# Platform-specific process isolation
if(APPLE)
    target_compile_definitions(layer2_plugins INTERFACE PLATFORM_MACOS)
elseif(UNIX)
    target_compile_definitions(layer2_plugins INTERFACE PLATFORM_LINUX)
elseif(WIN32)
    target_compile_definitions(layer2_plugins INTERFACE PLATFORM_WINDOWS)
endif()

# Source files
target_sources(layer2_plugins INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/iplugin_scanner.h>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/plugin_scanner.cpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/scanner_process.h>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/scanner_process.cpp>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/plugin_cache.h>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/plugin_validator.h>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/plugin_validator.cpp>
)

