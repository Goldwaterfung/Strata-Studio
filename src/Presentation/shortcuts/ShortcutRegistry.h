#pragma once

#include <QKeySequence>
#include <QString>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace presentation::shortcuts {

enum class ShortcutAction : uint16_t {
    // Global Window & Navigation
    Global_ToggleArrangement,   // F5 / Alt+1
    Global_TogglePianoRoll,     // F7 / Alt+2
    Global_ToggleMixer,         // F9 / Alt+3
    Global_ToggleMetronome,     // K / Ctrl+M
    Global_SaveProject,         // Ctrl+S
    Global_SaveProjectAs,       // Ctrl+Shift+S
    Global_OpenProject,         // Ctrl+O
    Global_Undo,                // Ctrl+Z
    Global_Redo,                // Ctrl+Shift+Z / Ctrl+Y

    // Playlist / Arrangement Tools
    Playlist_ToolDraw,          // P
    Playlist_ToolPaint,         // B
    Playlist_ToolDelete,        // D
    Playlist_ToolMute,          // T
    Playlist_ToolSlipEdit,      // S
    Playlist_ToolSlice,         // C
    Playlist_ToolSelect,        // E
    Playlist_ToolZoomSelect,    // Shift+Z
    Playlist_ToolPlaySelected,  // Y
    Playlist_ToggleAutomation,  // A
    Playlist_OpenProjectPicker, // Alt+P
    Playlist_OpenExportDialog,  // Ctrl+R
    Playlist_CopySelected,      // Ctrl+C
    Playlist_PasteSelected,     // Ctrl+V
    Playlist_CutSelected,       // Ctrl+X
    Playlist_SelectAll,         // Ctrl+A

    // Piano Roll Commands
    PianoRoll_Quantize,         // Q / Ctrl+Q
    PianoRoll_SelectAll,        // Ctrl+A
    PianoRoll_CopySelected,     // Ctrl+C
    PianoRoll_PasteSelected,    // Ctrl+V
    PianoRoll_CutSelected,      // Ctrl+X
    PianoRoll_Undo,             // Ctrl+Z
    PianoRoll_Redo,             // Ctrl+Shift+Z / Ctrl+Y

    // Mixer Commands
    Mixer_ToggleMute,           // M
    Mixer_ToggleSolo,           // S
    Mixer_ToggleRecordArm,      // C / Shift+R
    Mixer_CreateTrack,          // Ctrl+Shift+N
    Mixer_DeleteTrack,          // Ctrl+Shift+Delete
    Mixer_ClearAllSolos         // Alt+S / Ctrl+Alt+S
};

enum class ShortcutCategory : uint8_t {
    Global,
    Playlist,
    PianoRoll,
    Mixer
};

struct ShortcutInfo {
    ShortcutAction action;
    ShortcutCategory category;
    const char* idStr;
    const char* displayName;
    const char* description;
    const char* defaultKeySequence;
};

// Singleton / Global Registry for DAW Shortcut metadata & key mapping
class ShortcutRegistry {
public:
    static ShortcutRegistry& instance();

    ShortcutRegistry();

    // Get current key sequence assigned to an action
    [[nodiscard]] QKeySequence getSequence(ShortcutAction action) const;

    // Get metadata description for an action
    [[nodiscard]] std::optional<ShortcutInfo> getInfo(ShortcutAction action) const;

    // Set custom key sequence for an action
    void setSequence(ShortcutAction action, const QKeySequence& sequence);

    // Reset all shortcuts to factory defaults
    void resetToDefaults();

private:
    std::unordered_map<ShortcutAction, QKeySequence> m_activeSequences;
};

} // namespace presentation::shortcuts
