# Middle Bridge Layer
cmake_minimum_required(VERSION 3.20)

add_library(middle_bridge STATIC
    tracks/track_pipeline_builder.cpp
    boot/boot_controller.cpp
    telemetry/metering_provider.cpp
    engine/hardware_settings_facade.cpp
    tracks/track_controller.cpp
    tracks/track_lifecycle_controller.cpp
    tracks/track_mixer_controller.cpp
    tracks/track_plugin_controller.cpp
    tracks/track_routing_controller.cpp
    tracks/track_ui_state_builder.cpp
    timeline/timeline_controller.cpp
    timeline/arrangement_controller.cpp
    automation/automation_controller.cpp
    automation/automation_recording_gateway.cpp
    telemetry/waveform_cache_provider.cpp
    telemetry/pattern_data_provider.cpp
    project/session_manager.cpp
    project/project_lifecycle_controller.cpp
    recording/recording_controller.cpp
    browser/browser_controller.cpp
    engine/input_mode_controller.cpp
    project/workspace_controller.cpp
    midi/midi_editor_controller.cpp
    timeline/arrangement_manager_controller.cpp
    engine/render_controller.cpp
)

target_include_directories(middle_bridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(middle_bridge PUBLIC 
    common_utils
    layer1_filesystem
    layer2_memory
    layer3_streaming
    layer3_scheduler
    layer5_project_session
    layer5_track_manager
    layer5_automation
    layer5_playlist
    layer5_midi_sequencer
    layer5_command_history
    layer5_recording
    layer6_codecs
    layer6_registry
    layer6_analysis
    layer6_waveforms
    layer6_intake
    layer6_browser
    layer6_presets
    layer6_library
    layer6_export
    dsp_sampler
    dsp_sequencer
    dsp_sine_synth
    dsp_latency
    dsp_buses
    dsp_audio_input
    dsp_monitor_switch
    layer4_channelstrip
    layer4_plugins
    layer4_sends
    layer4_panner
    layer4_analysis
    layer4_tracks
)
