# Layer7: views
cmake_minimum_required(VERSION 3.20)

add_library(layer7_views STATIC
    ../shortcuts/ShortcutRegistry.cpp
    ../shortcuts/ShortcutManager.cpp
    theme.cpp
    boot/splash_screen.cpp
    mixer/base_tactile_control.cpp
    mixer/rotary_dial.cpp
    mixer/cyber_fader.cpp
    mixer/telemetry_meter.cpp
    mixer/base_slot_widget.cpp
    mixer/send_slot_widget.cpp
    mixer/send_editor_popup.cpp
    mixer/send_slot_container.cpp
    mixer/plugin_slot_widget.cpp
    mixer/effect_slot_widget.cpp
    mixer/instrument_slot_widget.cpp
    mixer/input_slot_widget.cpp


    mixer/effect_slot_container.cpp
    mixer/instrument_slot_container.cpp
    mixer/plugin_editor_dialog.cpp
    mixer/channel_strip_widget.cpp
    mixer/mixer_window.cpp
    browser_window/BrowserNavigationView.cpp
    browser_window/BrowserSearchView.cpp
    browser_window/BrowserTreeView.cpp
    browser_window/BrowserPreviewDeck.cpp
    browser_window/BrowserWidget.cpp
    browser_window/BrowserItem.cpp
    top_control_panel/time_display.cpp
    top_control_panel/transport_controls.cpp
    top_control_panel/input_mode_controls.cpp
    top_control_panel/workspace_controls.cpp
    top_control_panel/top_control_panel.cpp
    main_window.cpp
    playlist/PlaylistWindow.cpp
    playlist/TrackLayout.cpp
    playlist/TempoTrackHeader.cpp
    playlist/TempoTrackCanvas.cpp
    # Phase 2 — PlaylistClipCanvas + ClipItems
    playlist/PlaylistClipCanvas.cpp
    playlist/clips/AudioClipItem.cpp
    playlist/clips/PatternClipItem.cpp
    playlist/clips/AutomationClipItem.cpp
    playlist/waveform/TileRenderWorker.cpp
    playlist/waveform/WaveformTileCache.cpp
    # Phase 3 — TrackHeaderView + TrackHeaderItem + AutomationTrackHeaderItem + TakesTrackHeaderItem
    playlist/TrackHeaderView.cpp
    playlist/TrackRowRenderer.cpp
    playlist/TakeRowRenderer.cpp
    playlist/AutomationRowRenderer.cpp
    # Phase 4 — PlaylistTimelineRuler
    playlist/PlaylistTimelineRuler.cpp
    # Phase 5 — PlaylistMenuButton
    playlist/PlaylistMenuButton.cpp
    # Phase 7 — PickerPanel
    playlist/PickerPanel.cpp
    # Phase 9 — PlaylistContextMenu
    playlist/PlaylistContextMenu.cpp
    playlist/dialogs/DAWInputDialog.cpp
    # Phase 10 — MiniPlaylistPreview
    playlist/MiniPlaylistPreview.cpp
    # Phase 11 — ProjectPickerScreen
    playlist/ProjectPickerScreen.cpp
    # Phase 8 — Dialogs
    playlist/dialogs/MergeArrangementDialog.cpp
    playlist/dialogs/RenderSettingsDialog.cpp
    playlist/dialogs/ParameterWindow.cpp
    settings/settings_dialog.cpp
    
    # MIDI Editing — Piano Roll View Components
    pianoroll_window/PianoRollCanvas.cpp
    pianoroll_window/VelocityLaneView.cpp
    pianoroll_window/ControllerLaneView.cpp
    pianoroll_window/PianoRollWindow.cpp
)

target_include_directories(layer7_views PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../controllers
)
target_link_libraries(layer7_views PUBLIC
    common_utils
    middle_bridge          # exposes src/Middle Bridge/ include path via PUBLIC
    Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Svg
)
