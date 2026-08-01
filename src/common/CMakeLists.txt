# Common utilities and primitives
cmake_minimum_required(VERSION 3.20)

# Common utilities library (header-only)
add_library(common_utils INTERFACE)
target_include_directories(common_utils INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/math
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
)

# Math utilities
add_library(common_math INTERFACE)
target_include_directories(common_math INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/math
)
target_link_libraries(common_math INTERFACE common_utils)

# DSP utilities
add_library(common_dsp INTERFACE)
target_include_directories(common_dsp INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/dsp
)
target_link_libraries(common_dsp INTERFACE common_utils common_math)

# Utility functions
add_library(common_utils_impl INTERFACE)
target_include_directories(common_utils_impl INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
)
target_link_libraries(common_utils_impl INTERFACE common_utils)

# Platform-specific optimizations
if(ENABLE_SIMD)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        # ARM NEON
    elseif(WIN32)
        target_compile_options(common_dsp INTERFACE /arch:AVX2)
    else()
        target_compile_options(common_dsp INTERFACE -mavx2 -mfma)
    endif()
endif()
