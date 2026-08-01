# tests/unit/musical_composition/CMakeLists.txt

cmake_minimum_required(VERSION 3.20)

add_executable(musical_composition_unit_tests
    automation_capture_engine_test.cpp
    undo_redo_subsystems_test.cpp
    session_metadata_keysig_test.cpp
    track_routing_comments_test.cpp
    plugin_state_serialization_test.cpp
    timeline_markers_calculations_test.cpp
    track_sidechain_test.cpp
)

target_include_directories(musical_composition_unit_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(musical_composition_unit_tests PRIVATE
    Catch2::Catch2WithMain
    layer5_automation
    layer5_track_manager
    layer5_project_session
    layer5_command_history
    layer3_automation
    layer2_memory
    dsp_sequencer
    common_dsp
    common_utils
)

# Register tests with CTest
include(Catch)
catch_discover_tests(musical_composition_unit_tests)
