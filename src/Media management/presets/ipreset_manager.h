#pragma once

#include "common/system_primitives.h"
#include <memory>
#include <vector>

namespace Layer2 {
    class IStringRegistry;
}

namespace MediaManagement {

/**
 * @brief Metadata for a saved preset.
 * 
 * True POD structure representing preset library metadata.
 * Uses IStringRegistry for all string-based fields.
 */
struct Preset {
    uint32_t nameId;            // Registry ID for preset name
    uint32_t categoryId;        // Registry ID for category
    uint32_t tags[8];           // Registry IDs for tags
    uint32_t numTags;           // Number of active tags
    uint32_t pluginId;          // Associated plugin type
    TrackID trackId;            // Source track if applicable
    uint32_t stateDataSize;     // Size of the DSP state blob
    uint64_t createdTime;       // Creation timestamp
    uint64_t modifiedTime;      // Last modification timestamp
};

static_assert(std::is_pod<Preset>::value, "Preset must be Plain Old Data");

/**
 * @brief Bridge interface to extract/apply state from/to active engine nodes.
 * 
 * This allows IPresetManager to implement quickSave/quickLoad by communicating
 * with Layer 5 (Musical Composition) and Layer 3 (Audio Engine).
 */
class IPluginStateBridge {
public:
    virtual ~IPluginStateBridge() = default;
    
    /**
     * @brief Get serialized state size for a plugin on a track.
     */
    virtual uint64_t getPluginStateSize(uint32_t pluginId, TrackID trackId) = 0;
    
    /**
     * @brief Extract current state from an active plugin.
     */
    virtual bool savePluginState(uint32_t pluginId, TrackID trackId, uint8_t* buffer, uint64_t size) = 0;
    
    /**
     * @brief Apply state to an active plugin.
     */
    virtual bool loadPluginState(uint32_t pluginId, TrackID trackId, const uint8_t* buffer, uint64_t size) = 0;
};

/**
 * @brief Interface for the Preset Manager service.
 * 
 * Handles the persistence, categorization, and retrieval of state blobs.
 * This is a library manager, node-agnostic by design.
 */
class IPresetManager {
public:
    virtual ~IPresetManager() = default;

    /**
     * @brief Save preset
     * @param preset Preset metadata
     * @param stateData Serialized state data (caller-provided)
     * @param stateDataSize Size of state data in bytes
     * @return True if preset was saved
     */
    virtual bool savePreset(const Preset& preset,
                           const uint8_t* stateData,
                           uint32_t stateDataSize) = 0;

    /**
     * @brief Load preset
     * @param nameId Preset name (string registry ID)
     * @param outPreset Output preset metadata (caller-provided)
     * @param outStateData Output state data buffer (caller-provided)
     * @param maxStateSize Maximum state data buffer size
     * @return True if preset was loaded
     */
    virtual bool loadPreset(uint32_t nameId,
                           Preset& outPreset,
                           uint8_t* outStateData,
                           uint32_t maxStateSize) const = 0;

    /**
     * @brief Delete preset
     * @param nameId Preset name (string registry ID)
     * @return True if preset was deleted
     */
    virtual bool deletePreset(uint32_t nameId) = 0;

    /**
     * @brief Rename preset
     * @param oldNameId Current preset name
     * @param newNameId New preset name
     * @return True if preset was renamed
     */
    virtual bool renamePreset(uint32_t oldNameId, uint32_t newNameId) = 0;

    // --- Query Operations ---

    /**
     * @brief Get total preset count
     */
    virtual uint32_t getPresetCount() const = 0;

    /**
     * @brief Get presets by category
     */
    virtual uint32_t getPresetsByCategory(uint32_t categoryId,
                                        Preset* outPresets,
                                        uint32_t maxPresets) const = 0;

    /**
     * @brief Get presets by tag
     */
    virtual uint32_t getPresetsByTag(uint32_t tagId,
                                    Preset* outPresets,
                                    uint32_t maxPresets) const = 0;

    /**
     * @brief Search presets
     */
    virtual uint32_t searchPresets(uint32_t queryId,
                                  Preset* outPresets,
                                  uint32_t maxPresets) const = 0;

    // --- Quick Save/Load ---

    /**
     * @brief Quick save current plugin/track state
     */
    virtual bool quickSave(uint32_t pluginId,
                          TrackID trackId,
                          uint32_t nameId) = 0;

    /**
     * @brief Quick load preset to plugin/track
     */
    virtual bool quickLoad(uint32_t pluginId,
                          TrackID trackId,
                          uint32_t nameId) = 0;

    /**
     * @brief Factory method.
     */
    static std::unique_ptr<IPresetManager> create(
        Layer2::IStringRegistry* registry,
        IPluginStateBridge* bridge = nullptr);
};

} // namespace MediaManagement
