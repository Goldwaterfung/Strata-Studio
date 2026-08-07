# tests/unit/Agentic layer/CMakeLists.txt

cmake_minimum_required(VERSION 3.20)

add_executable(agentic_layer_unit_tests
    analysis_handler_test.cpp
)

target_include_directories(agentic_layer_unit_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/Agentic layer"
)

target_link_libraries(agentic_layer_unit_tests PRIVATE
    Catch2::Catch2WithMain
    agentic_server
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
)

include(Catch)
catch_discover_tests(agentic_layer_unit_tests)
