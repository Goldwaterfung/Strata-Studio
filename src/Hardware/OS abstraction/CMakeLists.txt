# CMakeLists.txt
# Layer 1: Hardware/OS Abstraction
# This layer provides cross-platform abstraction for audio/MIDI I/O, filesystem, and threading

cmake_minimum_required(VERSION 3.20)

# =============================================================================
# LAYER 1 INTERFACE LIBRARY (headers only)
# =============================================================================

add_library(layer1_interface INTERFACE)
target_include_directories(layer1_interface INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(layer1_interface INTERFACE cxx_std_20)

# =============================================================================
# COMMON UTILITIES
# =============================================================================

add_library(layer1_common INTERFACE)
target_include_directories(layer1_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_sources(layer1_common INTERFACE
    common/layer1_primitives.h
    common/lock_free_queue.h
    common/platform_detection.h
    common/aligned_allocator.h
)
target_compile_features(layer1_common INTERFACE cxx_std_20)

# =============================================================================
# PLATFORM DETECTION
# =============================================================================

if(WIN32)
    set(LAYER1_PLATFORM "windows")
    set(LAYER1_PLATFORM_DEFINITIONS "WINDOWS_OS")
elseif(APPLE)
    set(LAYER1_PLATFORM "macos")
    set(LAYER1_PLATFORM_DEFINITIONS "MACOS_OS")
elseif(UNIX)
    set(LAYER1_PLATFORM "linux")
    set(LAYER1_PLATFORM_DEFINITIONS "LINUX_OS")
endif()

# =============================================================================
# AUDIO SUBSYSTEM
# =============================================================================

add_subdirectory(audio)

# =============================================================================
# MIDI SUBSYSTEM
# =============================================================================

add_subdirectory(midi)

# =============================================================================
# FILESYSTEM SUBSYSTEM
# =============================================================================

add_subdirectory(filesystem)

# =============================================================================
# THREADING SUBSYSTEM
# =============================================================================

add_subdirectory(threading)

# =============================================================================
# LAYER 1 AGGREGATION (all implementations)
# =============================================================================

add_library(layer1 INTERFACE)
target_link_libraries(layer1 INTERFACE
    layer1_audio
    layer1_midi
    layer1_filesystem
    layer1_threading
    layer1_common
)

# Export for Layer 2
target_include_directories(layer1 INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

# Compile definitions
target_compile_definitions(layer1 INTERFACE
    LAYER1_PLATFORM="${LAYER1_PLATFORM}"
    ${LAYER1_PLATFORM_DEFINITIONS}
)

# =============================================================================
# SIMD OPTIMIZATION FLAGS
# =============================================================================

option(ENABLE_SIMD "Enable SIMD optimizations for Layer 1" ON)

if(ENABLE_SIMD)
    if(MSVC)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
            target_compile_options(layer1 INTERFACE /arch:AVX2)
        endif()
    else()
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
            target_compile_options(layer1 INTERFACE -mavx2 -mfma)
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            # NEON is usually enabled by default on ARM64, but we can be explicit
            target_compile_options(layer1 INTERFACE -march=armv8-a+simd)
        endif()
    endif()
endif()

# =============================================================================
# TESTING
# =============================================================================

option(BUILD_LAYER1_TESTS "Build Layer 1 unit tests" OFF)

if(BUILD_LAYER1_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# =============================================================================
# INSTALLATION
# =============================================================================

if(INSTALL_DEVELOPMENT_FILES)
    install(DIRECTORY audio/ DESTINATION include/layer1/audio
        FILES_MATCHING PATTERN "*.h"
        COMPONENT Development
    )

    install(DIRECTORY midi/ DESTINATION include/layer1/midi
        FILES_MATCHING PATTERN "*.h"
        COMPONENT Development
    )

    install(DIRECTORY filesystem/ DESTINATION include/layer1/filesystem
        FILES_MATCHING PATTERN "*.h"
        COMPONENT Development
    )

    install(DIRECTORY threading/ DESTINATION include/layer1/threading
        FILES_MATCHING PATTERN "*.h"
        COMPONENT Development
    )

    install(DIRECTORY common/ DESTINATION include/layer1/common
        FILES_MATCHING PATTERN "*.h"
        COMPONENT Development
    )

    install(TARGETS layer1 layer1_interface
        ARCHIVE DESTINATION lib COMPONENT Development
        LIBRARY DESTINATION lib COMPONENT Development
    )
endif()
