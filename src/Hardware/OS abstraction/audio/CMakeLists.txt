# CMakeLists.txt
# Layer 1: Hardware/OS Abstraction - Audio Subsystem

cmake_minimum_required(VERSION 3.20)

# =============================================================================
# OPTIONS
# =============================================================================

# Option to use RtAudio (cross-platform audio library)
option(USE_RTAUDIO "Use RtAudio for cross-platform audio I/O" ON)

# Option to use native platform audio drivers
option(USE_NATIVE_AUDIO_DRIVERS "Use native platform audio drivers" OFF)

# =============================================================================
# AUDIO SUBSYSTEM INTERFACE
# =============================================================================

add_library(layer1_audio_interface INTERFACE)
target_include_directories(layer1_audio_interface INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(layer1_audio_interface INTERFACE
    iaudio_driver.h
    audio_format_converter.h
    rt_audio_wrapper.h
)
target_compile_features(layer1_audio_interface INTERFACE cxx_std_20)

set(LAYER1_AUDIO_SOURCES 
    audio_format_converter.cpp
)
set(LAYER1_AUDIO_LIBS "")

# =============================================================================
# RTAUDIO INTEGRATION
# =============================================================================

if(USE_RTAUDIO)
    if(TARGET RtAudio::rtaudio)
        target_link_libraries(layer1_audio_interface INTERFACE RtAudio::rtaudio)
        target_compile_definitions(layer1_audio_interface INTERFACE HAVE_RTAUDIO=1)
        message(STATUS "Using RtAudio via vcpkg/system target")
    else()
        message(WARNING "RtAudio enabled but target RtAudio::rtaudio not found")
    endif()
endif()

# =============================================================================
# PLATFORM-SPECIFIC IMPLEMENTATIONS
# =============================================================================

# Detect platform and add appropriate sources
if(WIN32)
    # Windows implementations
    if(USE_NATIVE_AUDIO_DRIVERS)
        list(APPEND LAYER1_AUDIO_SOURCES
            windows/wasapi_audio_driver.cpp
            windows/asio_audio_driver.cpp
            windows/direct_sound_driver.cpp
        )
        target_include_directories(layer1_audio_interface INTERFACE 
            ${CMAKE_SOURCE_DIR}/src/third_party/ASIOSDK/common
        )
        list(APPEND LAYER1_AUDIO_LIBS
            winmm
            ksuser
            ole32
        )
        target_compile_definitions(layer1_audio_interface INTERFACE
            WINDOWS_AUDIO=1
            USE_NATIVE_AUDIO_DRIVERS=1
        )
    endif()

    if(USE_RTAUDIO)
        target_compile_definitions(layer1_audio_interface INTERFACE __WINDOWS_WASAPI__ __WINDOWS_ASIO__ __WINDOWS_DS__)
        list(APPEND LAYER1_AUDIO_LIBS winmm ole32 ksuser dsound)
    endif()

elseif(APPLE)
    # macOS implementations
    if(USE_NATIVE_AUDIO_DRIVERS)
        list(APPEND LAYER1_AUDIO_SOURCES
            macos/core_audio_driver.cpp
            macos/aggregate_device_helper.mm
        )
        target_compile_definitions(layer1_audio_interface INTERFACE
            MACOS_AUDIO=1
            USE_NATIVE_AUDIO_DRIVERS=1
        )
    endif()

    # RtAudio on macOS needs these frameworks too
    if(USE_RTAUDIO OR USE_NATIVE_AUDIO_DRIVERS)
        list(APPEND LAYER1_AUDIO_LIBS
            "-framework CoreAudio"
            "-framework AudioToolbox"
            "-framework AudioUnit"
            "-framework Foundation"
            "-framework CoreFoundation"
        )
    endif()
    
    if(USE_RTAUDIO)
        target_compile_definitions(layer1_audio_interface INTERFACE __MACOSX_CORE__)
    endif()

endif()

# =============================================================================
# AUDIO DRIVER LIBRARY
# =============================================================================

add_library(layer1_audio STATIC ${LAYER1_AUDIO_SOURCES})
target_link_libraries(layer1_audio PUBLIC
    layer1_audio_interface
    layer1_common
    ${LAYER1_AUDIO_LIBS}
)

# Always include the factory
target_sources(layer1_audio PRIVATE
    audio_driver_factory.cpp
)

# Conditionally include RtAudio wrapper
if(USE_RTAUDIO)
    target_sources(layer1_audio PRIVATE
        rt_audio_wrapper.cpp
    )
endif()

# =============================================================================
# SIMD OPTIMIZATION FLAGS
# =============================================================================

option(ENABLE_AUDIO_SIMD "Enable SIMD optimizations for audio processing" ON)

if(ENABLE_AUDIO_SIMD)
    if(MSVC)
        target_compile_options(layer1_audio PRIVATE /arch:AVX2)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        # NEON is default
    else()
        target_compile_options(layer1_audio PRIVATE -mavx2 -mfma)
    endif()
endif()

# =============================================================================
# INSTALLATION
# =============================================================================

if(INSTALL_DEVELOPMENT_FILES)
    install(TARGETS layer1_audio
        ARCHIVE DESTINATION lib COMPONENT Development
        LIBRARY DESTINATION lib COMPONENT Development
    )

    install(FILES iaudio_driver.h
        DESTINATION include/layer1/audio COMPONENT Development
    )
endif()

