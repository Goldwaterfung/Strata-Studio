# CMakeLists.txt
# Layer 1: Hardware/OS Abstraction - MIDI Subsystem

cmake_minimum_required(VERSION 3.20)

# =============================================================================
# MIDI SUBSYSTEM INTERFACE
# =============================================================================

add_library(layer1_midi_interface INTERFACE)
target_include_directories(layer1_midi_interface INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(layer1_midi_interface INTERFACE
    imidi_driver.h
    midi_queue.h
    midi_config.h
)
target_compile_features(layer1_midi_interface INTERFACE cxx_std_20)

# =============================================================================
# PLATFORM-SPECIFIC IMPLEMENTATIONS
# =============================================================================

set(LAYER1_MIDI_SOURCES 
    midi_factory.cpp 
    rtmidi_driver.cpp
)

set(LAYER1_MIDI_LIBS "")

if(WIN32)
    list(APPEND LAYER1_MIDI_SOURCES
        windows/winmm_midi_driver.cpp
    )
    list(APPEND LAYER1_MIDI_LIBS winmm)
elseif(APPLE)
    list(APPEND LAYER1_MIDI_SOURCES
        macos/coremidi_driver.cpp
    )
    list(APPEND LAYER1_MIDI_LIBS "-framework CoreMIDI" "-framework CoreFoundation")
endif()

# =============================================================================
# MIDI DRIVER LIBRARY
# =============================================================================

add_library(layer1_midi STATIC ${LAYER1_MIDI_SOURCES})

target_link_libraries(layer1_midi PUBLIC
    layer1_midi_interface
    layer1_common
    ${LAYER1_MIDI_LIBS}
)

if(TARGET RtMidi::rtmidi)
    target_link_libraries(layer1_midi PRIVATE RtMidi::rtmidi)
    target_compile_definitions(layer1_midi PRIVATE HAVE_RTMIDI)
endif()

# =============================================================================
# INSTALLATION
# =============================================================================

if(INSTALL_DEVELOPMENT_FILES)
    install(TARGETS layer1_midi
        ARCHIVE DESTINATION lib COMPONENT Development
        LIBRARY DESTINATION lib COMPONENT Development
    )

    install(FILES 
        imidi_driver.h
        midi_config.h
        DESTINATION include/layer1/midi COMPONENT Development
    )
endif()
