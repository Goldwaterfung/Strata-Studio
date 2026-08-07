# tests/unit/Middle Bridge/CMakeLists.txt

cmake_minimum_required(VERSION 3.20)

add_executable(middle_bridge_unit_tests
    metering_provider_test.cpp
    hardware_settings_facade_test.cpp
    track_controller_test.cpp
    track_lifecycle_controller_test.cpp
    track_mixer_controller_test.cpp
    track_ui_state_builder_test.cpp
    timeline_controller_test.cpp
    arrangement_controller_test.cpp
    automation_controller_test.cpp
    waveform_cache_provider_test.cpp
    session_management_test.cpp
    midi_editor_controller_test.cpp
    track_bridge_sidechain_test.cpp
    analysis_controller_test.cpp
)

target_include_directories(middle_bridge_unit_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

# Find Qt for UI testing components (QApplication, QWidget)
find_package(Qt6 COMPONENTS Widgets REQUIRED)

target_link_libraries(middle_bridge_unit_tests PRIVATE
    Catch2::Catch2WithMain
    middle_bridge
    layer5_project_session
    layer4_plugins
    layer4_channelstrip
    layer4_panner
    layer4_sends

    dsp_latency
    dsp_sine_synth
    layer3_engine
    layer3_plugin
    layer2_bridges
    layer2_memory
    layer1_common
    Qt6::Widgets
)

# Register tests with CTest
include(Catch)
catch_discover_tests(middle_bridge_unit_tests)
