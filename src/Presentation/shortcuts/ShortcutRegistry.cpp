#include "ShortcutRegistry.h"

namespace presentation::shortcuts {

namespace {

constexpr std::array<ShortcutInfo, 38> kDefaultShortcutTable = {{
    // Global Navigation & Commands
    { ShortcutAction::Global_ToggleArrangement,   ShortcutCategory::Global, "global.view.arrangement",   "Focus Arrangement",   "Toggle Arrangement Window visibility", "F5" },
    { ShortcutAction::Global_TogglePianoRoll,     ShortcutCategory::Global, "global.view.pianoroll",     "Focus Piano Roll",   "Toggle Piano Roll Window visibility",   "F7" },
    { ShortcutAction::Global_ToggleMixer,         ShortcutCategory::Global, "global.view.mixer",         "Focus Mixer",        "Toggle Mixer Window visibility",        "F9" },
    { ShortcutAction::Global_ToggleMetronome,     ShortcutCategory::Global, "global.transport.metronome","Toggle Metronome",   "Toggle Metronome Click track",          "K" },
    { ShortcutAction::Global_SaveProject,         ShortcutCategory::Global, "global.project.save",       "Save Project",       "Save active project to disk",           "Ctrl+S" },
    { ShortcutAction::Global_SaveProjectAs,       ShortcutCategory::Global, "global.project.save_as",    "Save Project As",    "Save project under a new path",         "Ctrl+Shift+S" },
    { ShortcutAction::Global_OpenProject,         ShortcutCategory::Global, "global.project.open",       "Open Project",       "Open existing project file",            "Ctrl+O" },
    { ShortcutAction::Global_Undo,                ShortcutCategory::Global, "global.edit.undo",          "Undo",               "Undo last action",                      "Ctrl+Z" },
    { ShortcutAction::Global_Redo,                ShortcutCategory::Global, "global.edit.redo",          "Redo",               "Redo last undone action",               "Ctrl+Shift+Z" },

    // Playlist / Arrangement Tools
    { ShortcutAction::Playlist_ToolDraw,          ShortcutCategory::Playlist, "playlist.tool.draw",      "Draw Tool",          "Activate Draw tool",                    "P" },
    { ShortcutAction::Playlist_ToolPaint,         ShortcutCategory::Playlist, "playlist.tool.paint",     "Paint Tool",         "Activate Paint tool",                   "B" },
    { ShortcutAction::Playlist_ToolDelete,        ShortcutCategory::Playlist, "playlist.tool.delete",    "Delete Tool",        "Activate Delete tool",                  "D" },
    { ShortcutAction::Playlist_ToolMute,          ShortcutCategory::Playlist, "playlist.tool.mute",      "Mute Tool",          "Activate Mute tool",                    "T" },
    { ShortcutAction::Playlist_ToolSlipEdit,      ShortcutCategory::Playlist, "playlist.tool.slip",      "Slip Edit Tool",     "Activate Slip Edit tool",               "S" },
    { ShortcutAction::Playlist_ToolSlice,         ShortcutCategory::Playlist, "playlist.tool.slice",     "Slice Tool",         "Activate Slice tool",                   "C" },
    { ShortcutAction::Playlist_ToolSelect,        ShortcutCategory::Playlist, "playlist.tool.select",    "Select Tool",        "Activate Select tool",                  "E" },
    { ShortcutAction::Playlist_ToolZoomSelect,    ShortcutCategory::Playlist, "playlist.tool.zoom",      "Zoom Select Tool",   "Activate Zoom Select tool",             "Shift+Z" },
    { ShortcutAction::Playlist_ToolPlaySelected,  ShortcutCategory::Playlist, "playlist.tool.play",      "Play Selected Tool", "Activate Play Selected tool",          "Y" },
    { ShortcutAction::Playlist_ToggleAutomation,  ShortcutCategory::Playlist, "playlist.view.automation",  "Toggle Automation",  "Toggle Automation Lanes visibility",    "A" },
    { ShortcutAction::Playlist_OpenProjectPicker, ShortcutCategory::Playlist, "playlist.view.picker",      "Project Picker",     "Open Project Picker screen",            "Alt+P" },
    { ShortcutAction::Playlist_OpenExportDialog,  ShortcutCategory::Playlist, "playlist.project.export",   "Export Audio",       "Open Render / Export Dialog",           "Ctrl+R" },
    { ShortcutAction::Playlist_CopySelected,      ShortcutCategory::Playlist, "playlist.edit.copy",        "Copy Clips",         "Copy selected clips",                   "Ctrl+C" },
    { ShortcutAction::Playlist_PasteSelected,     ShortcutCategory::Playlist, "playlist.edit.paste",       "Paste Clips",        "Paste clips at playhead",               "Ctrl+V" },
    { ShortcutAction::Playlist_CutSelected,       ShortcutCategory::Playlist, "playlist.edit.cut",         "Cut Clips",          "Cut selected clips",                    "Ctrl+X" },
    { ShortcutAction::Playlist_SelectAll,         ShortcutCategory::Playlist, "playlist.edit.select_all",  "Select All Clips",   "Select all clips in timeline",          "Ctrl+A" },

    // Piano Roll Commands
    { ShortcutAction::PianoRoll_Quantize,         ShortcutCategory::PianoRoll, "pianoroll.edit.quantize",   "Quantize Notes",    "Quantize selected MIDI notes",          "Q" },
    { ShortcutAction::PianoRoll_SelectAll,        ShortcutCategory::PianoRoll, "pianoroll.edit.select_all", "Select All Notes",  "Select all MIDI notes",                 "Ctrl+A" },
    { ShortcutAction::PianoRoll_CopySelected,     ShortcutCategory::PianoRoll, "pianoroll.edit.copy",       "Copy Notes",        "Copy selected MIDI notes",              "Ctrl+C" },
    { ShortcutAction::PianoRoll_PasteSelected,    ShortcutCategory::PianoRoll, "pianoroll.edit.paste",      "Paste Notes",       "Paste MIDI notes",                      "Ctrl+V" },
    { ShortcutAction::PianoRoll_CutSelected,      ShortcutCategory::PianoRoll, "pianoroll.edit.cut",        "Cut Notes",         "Cut selected MIDI notes",               "Ctrl+X" },
    { ShortcutAction::PianoRoll_Undo,             ShortcutCategory::PianoRoll, "pianoroll.edit.undo",       "Undo Note Edit",    "Undo last MIDI edit",                   "Ctrl+Z" },
    { ShortcutAction::PianoRoll_Redo,             ShortcutCategory::PianoRoll, "pianoroll.edit.redo",       "Redo Note Edit",    "Redo last MIDI edit",                   "Ctrl+Shift+Z" },

    // Mixer Commands
    { ShortcutAction::Mixer_ToggleMute,           ShortcutCategory::Mixer,    "mixer.track.mute",          "Mute Track",         "Mute focused mixer track",              "M" },
    { ShortcutAction::Mixer_ToggleSolo,           ShortcutCategory::Mixer,    "mixer.track.solo",          "Solo Track",         "Solo focused mixer track",              "S" },
    { ShortcutAction::Mixer_ToggleRecordArm,      ShortcutCategory::Mixer,    "mixer.track.arm",           "Arm Track",          "Arm focused track for recording",       "C" },
    { ShortcutAction::Mixer_CreateTrack,          ShortcutCategory::Mixer,    "mixer.track.create",        "Create Track",       "Create new audio/MIDI track",           "Ctrl+Shift+N" },
    { ShortcutAction::Mixer_DeleteTrack,          ShortcutCategory::Mixer,    "mixer.track.delete",        "Delete Track",       "Delete focused track",                  "Ctrl+Shift+Delete" },
    { ShortcutAction::Mixer_ClearAllSolos,        ShortcutCategory::Mixer,    "mixer.track.clear_solos",   "Clear Solos",        "Clear all track solo states",           "Alt+S" }
}};

} // namespace

ShortcutRegistry& ShortcutRegistry::instance() {
    static ShortcutRegistry reg;
    return reg;
}

ShortcutRegistry::ShortcutRegistry() {
    resetToDefaults();
}

QKeySequence ShortcutRegistry::getSequence(ShortcutAction action) const {
    auto it = m_activeSequences.find(action);
    if (it != m_activeSequences.end()) {
        return it->second;
    }
    return QKeySequence();
}

std::optional<ShortcutInfo> ShortcutRegistry::getInfo(ShortcutAction action) const {
    for (const auto& item : kDefaultShortcutTable) {
        if (item.action == action) {
            return item;
        }
    }
    return std::nullopt;
}

void ShortcutRegistry::setSequence(ShortcutAction action, const QKeySequence& sequence) {
    m_activeSequences[action] = sequence;
}

void ShortcutRegistry::resetToDefaults() {
    m_activeSequences.clear();
    for (const auto& item : kDefaultShortcutTable) {
        if (item.action == ShortcutAction::Global_Undo || item.action == ShortcutAction::PianoRoll_Undo) {
            m_activeSequences[item.action] = QKeySequence(QKeySequence::Undo);
        } else if (item.action == ShortcutAction::Global_Redo || item.action == ShortcutAction::PianoRoll_Redo) {
            m_activeSequences[item.action] = QKeySequence(QKeySequence::Redo);
        } else {
            m_activeSequences[item.action] = QKeySequence(item.defaultKeySequence);
        }
    }
}

} // namespace presentation::shortcuts
