# Main Application
cmake_minimum_required(VERSION 3.20)

# Main executable
add_executable(${PROJECT_EXECUTABLE_NAME} MACOSX_BUNDLE WIN32
    main.cpp
    application.cpp
    composition_root.cpp
    pipeline_builders/track_pipeline_builder_impl.cpp
    pipeline_builders/preview_pipeline_builder_impl.cpp
    ${CMAKE_SOURCE_DIR}/src/Presentation/resources.qrc
)

# Link all layers (dependency order matters)
target_link_libraries(${PROJECT_EXECUTABLE_NAME} PRIVATE
    # Layer 7
    layer7_views
    layer7_controllers
    # Layer 1
    layer1_audio
    layer1_midi
    # Layer 8
    agentic_server
    # Middle Bridge
    middle_bridge
    # Layer 6 Media Management
    layer5_command_history
    layer5_recording
    layer6_registry
    layer6_codecs
    layer6_analysis
    layer6_waveforms
    layer6_intake
    layer6_browser
    layer6_presets
    layer6_library
    layer6_export
    layer6_recording
    # Layer 3
    layer3_scheduler
    layer3_transport
    layer3_engine
    layer3_plugin
    layer3_automation_impl
    layer3_sidechain
    layer3_sidechain_impl
    # Common
    common_utils
    common_dsp
    dsp_audio_input
    dsp_monitor_switch
    layer4_tracks
    # External libraries
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    Threads::Threads
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::Svg
    Qt6::VirtualKeyboard
)

# Platform-specific libraries
if(APPLE)
    target_link_libraries(${PROJECT_EXECUTABLE_NAME} PRIVATE
        "-framework CoreFoundation"
        "-framework CoreServices"
    )
elseif(WIN32)
    target_link_libraries(${PROJECT_EXECUTABLE_NAME} PRIVATE
        setupapi
        ole32
        shell32
    )
elseif(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_EXECUTABLE_NAME} PRIVATE
        dl
        rt
    )
endif()

# Include directories
target_include_directories(${PROJECT_EXECUTABLE_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Set output directory and bundle properties
set_target_properties(${PROJECT_EXECUTABLE_NAME} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.stratastudio.daw"
    MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_DISPLAY_NAME}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Installation
install(TARGETS ${PROJECT_EXECUTABLE_NAME}
    BUNDLE DESTINATION . COMPONENT Application
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Application
)


