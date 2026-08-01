# tests/unit/DSP nodes/CMakeLists.txt

add_executable(audio_sequencer_node_test audio_sequencer_node_test.cpp)
target_link_libraries(audio_sequencer_node_test PRIVATE
    Catch2::Catch2WithMain
    dsp_sequencer
    layer3_streaming
    layer3_scheduler
    layer2_memory
    layer2_bridges
    layer1_filesystem
    layer1_threading
)
add_test(NAME audio_sequencer_node_test COMMAND audio_sequencer_node_test)

add_executable(midi_sequencer_node_test midi_sequencer_node_test.cpp)
target_link_libraries(midi_sequencer_node_test PRIVATE
    Catch2::Catch2WithMain
    dsp_sequencer
    layer3_streaming
    layer3_scheduler
    layer2_memory
    layer2_bridges
    layer1_filesystem
    layer1_threading
)
add_test(NAME midi_sequencer_node_test COMMAND midi_sequencer_node_test)

add_executable(monitor_switch_node_test monitor_switch_node_test.cpp)
target_link_libraries(monitor_switch_node_test PRIVATE
    Catch2::Catch2WithMain
    dsp_monitor_switch
    layer2_memory
    layer2_bridges
    layer1_threading
)
add_test(NAME monitor_switch_node_test COMMAND monitor_switch_node_test)

add_executable(latency_node_test latency_node_test.cpp)
target_link_libraries(latency_node_test PRIVATE
    Catch2::Catch2WithMain
    dsp_latency
    layer2_memory
    layer2_bridges
    layer1_threading
)
add_test(NAME latency_node_test COMMAND latency_node_test)

