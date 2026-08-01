// src/Middle Bridge/browser_primitives.h
#pragma once

#include "common/system_primitives.h"
#include <cstdint>
#include <type_traits>

namespace bridge {

enum class BrowserTab : uint8_t {
    AllFolders = 0,
    CurrentProject = 1,
    PluginDatabase = 2,
    Favorites = 3,
    Count
};

enum class BrowserItemType : uint8_t {
    Folder,
    AudioFile,
    MidiFile,
    PresetFile,
    ProjectFile,
    ScoreFile,
    PluginEffect,
    PluginGenerator
};

struct BrowserItem {
    uint32_t stringId;      // String handle from StringRegistry for path/name
    MediaID mediaId;        // Valid for files and assets, null handle for system folders
    BrowserItemType type;   // Item category
    uint32_t colorARGB;     // Visual highlight color
    bool isFavorite;        // Starred state
};

// Compile-time assertions for POD verification
static_assert(std::is_pod<BrowserItem>::value, "BrowserItem must be Plain Old Data");

} // namespace bridge
