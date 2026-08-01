cmake_minimum_required(VERSION 3.20)

# vcpkg integration (must be before project())
if(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
endif()

project(StrataStudio VERSION 1.0.0 LANGUAGES CXX)

# Centralized Naming Variables
set(PROJECT_DISPLAY_NAME "Strata Studio")
string(TOLOWER "${PROJECT_NAME}" PROJECT_NAME_LOWER)
set(PROJECT_EXECUTABLE_NAME "strata_studio" CACHE STRING "Executable target name")
set(PROJECT_NAMESPACE "${PROJECT_NAME_LOWER}" CACHE STRING "Target alias namespace")

# Generate project_config.h from template
configure_file(
    "${CMAKE_SOURCE_DIR}/src/common/project_config.h.in"
    "${CMAKE_BINARY_DIR}/generated/project_config.h"
    @ONLY
)

include_directories("${CMAKE_BINARY_DIR}/generated")

# C++20 Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build Options
option(BUILD_TESTS "Build unit tests" OFF)
option(BUILD_PERFORMANCE_TESTS "Build performance tests" OFF)
option(ENABLE_SIMD "Enable SIMD optimizations" ON)
option(USE_ASAN "Enable Address Sanitizer" OFF)
option(USE_TSAN "Enable Thread Sanitizer" OFF)
option(BUILD_PLUGINS "Build plugin host support" ON)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Qt Automatic Compiling Options
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Platform Detection
if(APPLE)
    set(PLATFORM_NAME "macos")
    set(PLATFORM_MACOS TRUE)
    # macOS specific flags
    set(CMAKE_MACOS_RPATH ON)
    # Enable ARC (if needed for some dependencies)
    # set(CMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES)
elseif(WIN32)
    set(PLATFORM_NAME "windows")
    set(PLATFORM_WINDOWS TRUE)
    # Windows specific definitions
    add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS)
elseif(UNIX)
    message(FATAL_ERROR "Linux platform support has been removed. Only macOS and Windows are supported.")
else()
    message(FATAL_ERROR "Unsupported operating system. Only macOS and Windows are supported.")
endif()

# Compiler Flags
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    set(CLANG TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(GCC TRUE)
elseif(MSVC)
    set(MSVC TRUE)
endif()

# Warning flags
if(MSVC)
    add_compile_options(/W4 /WX)
    # Additional safety checks for MSVC
    add_compile_options(/sdl)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
    # Additional warning flags for Clang/GCC
    add_compile_options(-Wshadow -Wold-style-cast -Wcast-align)
    add_compile_options(-Wunused -Woverloaded-virtual)
    add_compile_options(-Wconversion -Wsign-conversion)
    add_compile_options(-Wnull-dereference -Wdouble-promotion)
    add_compile_options(-Wformat=2)
    
    if(GCC)
        add_compile_options(-Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wuseless-cast)
    endif()
    
    if(CLANG)
        # Clang specific flags if any
    endif()
endif()

# Sanitizer flags
if(USE_ASAN)
    if(MSVC)
        message(WARNING "ASAN not fully supported on MSVC, may need manual configuration")
    else()
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    endif()
endif()

if(USE_TSAN)
    if(MSVC)
        message(WARNING "TSAN not supported on MSVC")
    else()
        add_compile_options(-fsanitize=thread)
        add_link_options(-fsanitize=thread)
    endif()
endif()

# SIMD options
if(ENABLE_SIMD)
    if(MSVC)
        add_compile_options(/arch:AVX2)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        # ARM NEON is generally enabled by default on Apple Silicon
        message(STATUS "SIMD: Using ARM NEON (default for ARM64)")
    else()
        add_compile_options(-mavx2 -mfma)
    endif()
    add_definitions(-DENABLE_SIMD=1)
else()
    add_definitions(-DENABLE_SIMD=0)
endif()

# Release build optimizations
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O2 -g -DNDEBUG")

# Debug build flags
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")

# Find required packages
find_package(Threads REQUIRED)

# Find Qt6 Components
find_package(Qt6 COMPONENTS Widgets Core Gui Svg VirtualKeyboard REQUIRED)

# Dependencies via vcpkg/system
find_package(RtAudio CONFIG REQUIRED)
find_package(rtmidi CONFIG REQUIRED)
find_package(SndFile REQUIRED)
find_package(SampleRate REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(libebur128 CONFIG QUIET)

# Testing frameworks
if(BUILD_TESTS)
    find_package(Catch2 CONFIG REQUIRED)
    enable_testing()
endif()

if(BUILD_PERFORMANCE_TESTS)
    find_package(benchmark CONFIG REQUIRED)
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/src/common)
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/src/third_party/eigen-5.0.0)

# Layer dependencies (each layer depends on lower layers)
add_subdirectory(src/common)

# Layer 1: Hardware/OS Abstraction
add_subdirectory("src/Hardware/OS abstraction")

# Layer 2: Core Infrastructure Services
add_subdirectory("src/Core infrastructure")

# Layer 3: Core Audio Engine
add_subdirectory("src/Core audio engine")

# Third-party: SoundTouch (must be before Layer 4 and Layer 6 consumers)
add_subdirectory(src/third_party/soundtouch)
add_subdirectory(src/third_party/rubberband-default)

# Layer 4: DSP Processing Nodes
add_subdirectory("src/DSP nodes/channelstrip")
add_subdirectory("src/DSP nodes/panner")
add_subdirectory("src/DSP nodes/plugins")
add_subdirectory("src/DSP nodes/sends")
add_subdirectory("src/DSP nodes/buses")
add_subdirectory("src/DSP nodes/timepitch")
add_subdirectory("src/DSP nodes/freeze")
add_subdirectory("src/DSP nodes/analysis")
add_subdirectory("src/DSP nodes/sampler")
add_subdirectory("src/DSP nodes/latency")
add_subdirectory("src/DSP nodes/sine_synth")
add_subdirectory("src/DSP nodes/audio_input")
add_subdirectory("src/DSP nodes/monitor_switch")
add_subdirectory("src/DSP nodes/sequencer")
add_subdirectory("src/DSP nodes/tracks")

# Layer 5: Musical & Project Composition
add_subdirectory(src/musical_composition)

# Layer 6: Media Management & Analysis
add_subdirectory("src/Media management/registry")
add_subdirectory("src/Media management/codecs")
add_subdirectory("src/Media management/analysis")
add_subdirectory("src/Media management/waveforms")
add_subdirectory("src/Media management/intake")
add_subdirectory("src/Media management/browser")
add_subdirectory("src/Media management/presets")
add_subdirectory("src/Media management/library")
add_subdirectory("src/Media management/export")
add_subdirectory("src/Media management/recording")

# Middle Bridge Layer
add_subdirectory("src/Middle Bridge")

# Layer 7: Presentation Layer
add_subdirectory(src/Presentation/controllers)
add_subdirectory(src/Presentation/views)

# Layer 8: Agentic Layer
add_subdirectory("src/Agentic layer")

# Main Application
add_subdirectory(src/app)

# Tests
if(BUILD_TESTS)
    add_subdirectory(tests/unit)
endif()

#if(BUILD_PERFORMANCE_TESTS)
#    add_subdirectory(tests/performance)
#endif()

# Installation
include(GNUInstallDirs)
option(INSTALL_DEVELOPMENT_FILES "Install SDK headers and static libraries" OFF)

if(INSTALL_DEVELOPMENT_FILES)
    install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/strata-studio
            FILES_MATCHING PATTERN "*.h" COMPONENT Development)
endif()

# Packaging Configuration (CPack)
set(CPACK_COMPONENTS_ALL Application)
set(CPACK_PACKAGE_NAME "${PROJECT_DISPLAY_NAME}")
set(CPACK_PACKAGE_VENDOR "Strata Studio Team")
set(CPACK_PACKAGE_VERSION_MAJOR 1)
set(CPACK_PACKAGE_VERSION_MINOR 0)
set(CPACK_PACKAGE_VERSION_PATCH 0)
set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Professional Agent-Based DAW")

if(APPLE)
    set(CPACK_GENERATOR "productbuild")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/Applications")
    set(CPACK_PRODUCTBUILD_IDENTITY_NAME "" CACHE STRING "Code signing identity for productbuild")
elseif(WIN32)
    set(CPACK_GENERATOR "NSIS;INNOSETUP")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "Strata Studio")
endif()

include(CPack)

# Print configuration summary
message(STATUS "")
message(STATUS "${PROJECT_NAME} Configuration Summary:")
message(STATUS "  Platform: ${PLATFORM_NAME}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  SIMD Enabled: ${ENABLE_SIMD}")
message(STATUS "  ASAN Enabled: ${USE_ASAN}")
message(STATUS "  TSAN Enabled: ${USE_TSAN}")
message(STATUS "  Build Tests: ${BUILD_TESTS}")
#message(STATUS "  Build Performance Tests: ${BUILD_PERFORMANCE_TESTS}")
message(STATUS "  Build Plugin Support: ${BUILD_PLUGINS}")
message(STATUS "")

