# Media Management Unit Tests
cmake_minimum_required(VERSION 3.20)

add_executable(media_registry_test media_registry_test.cpp)
add_executable(media_waveform_test waveform_renderer_test.cpp)
add_executable(media_roundtrip_test real_audio_roundtrip_test.cpp real_multi_audio_roundtrip_test.cpp)

target_link_libraries(media_registry_test PRIVATE 
    Catch2::Catch2WithMain
    layer6_registry
)

target_link_libraries(media_waveform_test PRIVATE 
    Catch2::Catch2WithMain
    layer6_waveforms
)

add_executable(media_analysis_test audio_analysis_test.cpp)
target_link_libraries(media_analysis_test PRIVATE 
    Catch2::Catch2WithMain
    layer6_analysis
    layer2_memory
)

add_executable(media_final_test layer6_final_test.cpp)
target_link_libraries(media_final_test PRIVATE 
    Catch2::Catch2WithMain
    layer6_export
    layer6_presets
    layer6_library
    layer2_memory
    layer3_scheduler
)

target_link_libraries(media_roundtrip_test PRIVATE 
    Catch2::Catch2WithMain
    layer6_export
    layer6_codecs
    layer1_filesystem
    layer1_audio
    layer2_memory
    layer3_streaming
    layer3_scheduler
    layer3_engine
    layer3_transport
    layer5_track_manager
    layer5_automation
    layer5_playlist
    layer5_midi_sequencer
    layer5_command_history
    dsp_sampler
    dsp_sequencer
    dsp_latency
    dsp_buses
    layer4_channelstrip
    layer4_sends

    layer4_panner
    layer4_analysis
    layer4_tracks
)

add_executable(media_playback_manual real_audio_track_playback.cpp)
target_link_libraries(media_playback_manual PRIVATE 
    middle_bridge
    layer3_engine
    layer3_transport
    layer1_audio
)


# Use CTest
add_test(NAME MediaRegistryTest COMMAND media_registry_test)
add_test(NAME MediaWaveformTest COMMAND media_waveform_test)
add_test(NAME MediaAnalysisTest COMMAND media_analysis_test)
add_test(NAME MediaFinalTest COMMAND media_final_test)
add_test(NAME MediaRoundtripTest COMMAND media_roundtrip_test)
