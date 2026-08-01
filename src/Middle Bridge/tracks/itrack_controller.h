#pragma once
#include "common/system_primitives.h"
#include "musical_composition/musical_primitives.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "automation/iautomation_controller.h"
#include <vector>
#include <functional>

namespace bridge {

/**
 * @brief Maximum number of auxiliary send slots per track (Pre-fader + Post-fader each).
 *        Fixed capacity enforces POD compliance and predictable memory layout.
 */
constexpr uint32_t MAX_SEND_SLOTS = 8;
constexpr uint32_t MAX_PLUGIN_SLOTS = 8;

/**
 * @brief POD representation of a single plugin slot inside the insert effects slot chain.
 *        Safe to copy across layer boundaries.
 */
struct TrackInputUIState {
    bool     hasInputSlot;
    uint32_t mappedPhysicalInputIndex;
    uint32_t numChannels;
    char     inputName[MAX_NAME_LENGTH];
};

static_assert(std::is_trivially_copyable<TrackInputUIState>::value,
              "TrackInputUIState must be trivially copyable for cross-layer safety");

using AudioInputUIState = TrackInputUIState;

struct TrackInputOption {
    uint32_t optionId;
    uint32_t numChannels;
    std::string name;
};

using AudioInputChannelDescriptor = TrackInputOption;


struct PluginSlotUIState {
    NodeID               pluginNodeId;                      ///< Opaque DSP plugin node (insert_plugin_node)
    bool                 bypassed;                          ///< Bypass state of this effect slot
    uint8_t              _pad[7];                           ///< Explicit padding
    SidechainSlotUIState sidechain;                         ///< Sidechain routing state
    char                 pluginName[MAX_PLUGIN_NAME_LENGTH]; ///< Human-readable plugin name (e.g. "Reverb")
};

static_assert(std::is_trivially_copyable<PluginSlotUIState>::value,
              "PluginSlotUIState must be trivially copyable for cross-layer safety");

/**
 * @brief POD representation of a single auxiliary send slot state.
 *        Safe to copy across layer boundaries (no pointers, no virtual methods).
 *
 * Design:
 *  - preFaderSends[] tap from the pre-fader panner node (NODE_TYPE_PANNER).
 *  - postFaderSends[] tap from the post-fader send node (NODE_TYPE_SEND).
 *  - destinationNodeId: the NodeID of the target AUX or Bus node.
 *  - destinationName: cached human-readable label populated by TrackController.
 */
struct SendSlotUIState {
    NodeID  sendNodeId;             ///< Opaque DSP tap node (panner or send)
    NodeID  destinationNodeId;      ///< Target AUX/Bus node (invalid = unrouted)
    float   leveldB;                ///< Send level in dB (-180.0 = silence, 0.0 = unity)
    float   panPosition;            ///< Send panning: 0.0 = left, 0.5 = center, 1.0 = right
    bool    isEnabled;              ///< false = bypassed (signal blocked at tap)
    uint8_t _pad[3];                ///< Explicit padding for deterministic struct size
    char    destinationName[MAX_NAME_LENGTH]; ///< Human-readable bus name (e.g. "Reverb Bus")
};

static_assert(std::is_trivially_copyable<SendSlotUIState>::value,
              "SendSlotUIState must be trivially copyable for cross-layer safety");

struct AutomationSubLaneUIState {
    char parameterName[MAX_NAME_LENGTH];
    NodeID targetNodeId;
    uint32_t subNodeId;
    uint32_t parameterIndex;
    bool isExpanded;
    uint32_t heightPx;
    uint8_t recordMode; // Maps to ::AutomationMode
    uint8_t padding[3]; // Structure alignment padding
};

static_assert(std::is_trivially_copyable<AutomationSubLaneUIState>::value,
              "AutomationSubLaneUIState must be trivially copyable for cross-layer safety");

struct LastTweakedParameter {
    bool     isValid;
    NodeID   routingNodeId;
    uint32_t subNodeId;
    uint32_t paramIndex;
    char     paramName[64];
    float    lastValue;
};

static_assert(std::is_trivially_copyable<LastTweakedParameter>::value,
              "LastTweakedParameter must be trivially copyable for cross-layer safety");

/**
 * @brief Complete track state snapshot exported to the Presentation Layer (Layer 7).
 *        Must remain POD / trivially-copyable; no dynamic allocation permitted.
 */
struct TrackUIState {
    TrackID  trackId;
    char     name[MAX_NAME_LENGTH];
    uint32_t colorARGB;
    composition::TrackType type;
    TrackID  parentFolderId;
    uint32_t audioLanesCount = 1;

    // --- Channel Strip ---
    float faderLeveldB;         ///< Main fader gain in dB (−180 = silence, 0 = unity)
    float panPosition;          ///< Main pan: 0.0 (left) – 0.5 (centre) – 1.0 (right)
    bool  isMuted;
    bool  isSoloed;
    bool  isRecordArmed;        ///< Track is armed for recording
    bool  isInputMonitoring;    ///< Software input monitoring is active
    bool  isLocked;             ///< Track is locked
    bool  isSelected;           ///< Track is selected in UI
    uint8_t _pad_perf[6];       ///< Padding for alignment

    char comments[1024];        ///< Track comments annotation
    TrackID outputTargetTrackId; ///< Destination Track ID for routing

    NodeID channelStripNode;
    NodeID pannerNode;
    NodeID audioInputNode;
    NodeID midiInputNode;
    AutomationMode automationMode;
    uint8_t _pad_auto[7];

    // --- Real-time telemetry (written by IMeteringProvider 60 Hz) ---
    float meterLeftPeak;
    float meterRightPeak;

    // --- Auxiliary sends ---
    uint32_t        activePreFaderSendCount;            ///< Valid entries in preFaderSends[]
    SendSlotUIState preFaderSends[MAX_SEND_SLOTS];      ///< Pre-fader tap slots

    uint32_t        activePostFaderSendCount;           ///< Valid entries in postFaderSends[]
    SendSlotUIState postFaderSends[MAX_SEND_SLOTS];     ///< Post-fader tap slots

    // --- Input Slot (Audio or MIDI) ---
    bool              hasAudioInputSlot;
    AudioInputUIState audioInput;
    TrackInputUIState trackInput;

    // --- Instrument Slot ---
    bool              hasInstrumentSlot;                ///< True for TrackType::INSTRUMENT or TrackType::MIDI
    PluginSlotUIState instrument;                       ///< The loaded instrument details

    // --- Insert Plugins ---
    uint32_t          activePluginCount;                ///< Valid entries in plugins[]
    PluginSlotUIState plugins[MAX_PLUGIN_SLOTS];        ///< Insert effects slot chain

    // --- Sub-Lanes ---
    bool              isAutomationExpanded;
    bool              isTakesExpanded;
    uint32_t          activeSubLaneCount;
    AutomationSubLaneUIState subLanes[128];               ///< Max 128 parallel parameters visible

    // --- Last Tweaked Parameter ---
    LastTweakedParameter lastTweaked;
};

static_assert(std::is_trivially_copyable<TrackUIState>::value,
              "TrackUIState must be trivially copyable for cross-layer safety");

/**
 * @brief Lightweight POD representing dynamic track states.
 *        Used for high-frequency UI updates to bypass heavy lock/copy operations.
 */
struct TrackDynamicState {
    float faderLeveldB;         ///< Main fader gain in dB
    float panPosition;          ///< Main pan: 0.0 (left) - 0.5 (center) - 1.0 (right)
    bool  isMuted;
    bool  isSoloed;
    uint8_t _pad[2];            ///< Explicit padding for alignment
};

static_assert(std::is_trivially_copyable<TrackDynamicState>::value,
              "TrackDynamicState must be trivially copyable for cross-layer safety");

struct ParameterDescriptorCacheItem {
    NodeID routingNodeId;
    uint32_t subNodeId;
    uint32_t parameterIndex;
    ::ParameterInfo info;
};

static_assert(std::is_trivially_copyable<ParameterDescriptorCacheItem>::value,
              "ParameterDescriptorCacheItem must be trivially copyable for cross-layer safety");

/**
 * @brief Controller interface managing track lifecycles and channel strip operations.
 *
 * Layer 7 widgets must interact EXCLUSIVELY through this interface.
 * No DSP factory, kernel, or scheduler types are exposed here.
 */
class ITrackController {
public:
    virtual ~ITrackController() = default;

    // --- Parameter Cache Query ---
    virtual std::vector<ParameterDescriptorCacheItem> getCachedParameters(TrackID trackId) const = 0;

    // --- Track Lifecycle (Undo/Redo integrated via Layer 5) ---
    virtual TrackID addAudioTrack(const char* name, uint32_t channels, uint32_t colorARGB) = 0;
    virtual TrackID addInstrumentTrack(const char* name, uint32_t colorARGB) = 0;
    virtual TrackID addAuxTrack(const char* name, uint32_t colorARGB) = 0;
    virtual TrackID addFolderTrack(const char* name, uint32_t colorARGB) = 0;
    virtual void removeTrack(TrackID trackId) = 0;
    virtual void renameTrack(TrackID trackId, const char* name) = 0;
    virtual void setTrackComments(TrackID trackId, const char* comments) = 0;
    virtual void setTrackOutputRouting(TrackID trackId, TrackID destinationTrackId) = 0;
    virtual void setTrackColor(TrackID trackId, uint32_t colorARGB) = 0;
    virtual void moveTrack(TrackID trackId, uint32_t newPositionIndex, TrackID newParentFolderId) = 0;
    virtual void setTrackParentFolder(TrackID childTrackId, TrackID parentFolderId) = 0;
    virtual void setTrackMode(TrackID trackId, composition::TrackType mode) = 0;
    virtual TrackID cloneTrack(TrackID sourceId) = 0;
    virtual void muteAllClips(TrackID trackId, bool mute) = 0;

    // --- Main Channel Strip (Thread-safe via Layer 2 Mutations) ---
    virtual void setFaderGain(TrackID trackId, float gainLinear) = 0;
    virtual void setPan(TrackID trackId, float panPosition) = 0;
    virtual void setMute(TrackID trackId, bool mute) = 0;
    virtual void setSolo(TrackID trackId, bool solo) = 0;
    virtual void setRecordArmed(TrackID trackId, bool armed) = 0;
    virtual void setInputMonitoring(TrackID trackId, bool enabled) = 0;

    // --- Input Configuration (Audio / MIDI) ---
    virtual void setTrackInput(TrackID trackId, uint32_t optionId, uint32_t numChannels) = 0;
    virtual std::vector<TrackInputOption> getAvailableTrackInputs(TrackID trackId) const = 0;

    virtual void setTrackAudioInput(TrackID trackId, uint32_t mappedPhysicalInputIndex, uint32_t numChannels) = 0;
    virtual std::vector<AudioInputChannelDescriptor> getAvailableAudioInputs() const = 0;

    // --- Auxiliary Send Controls (Thread-safe; topology changes async on butler thread) ---
    /**
     * @param isPreFader  true = pre-fader send tap, false = post-fader send tap
     * @param sendIndex   Slot index [0, MAX_SEND_SLOTS)
     * @param gainLinear  Linear gain coefficient (0.0 = silence, 1.0 = unity)
     */
    virtual void setSendGain(TrackID trackId, bool isPreFader,
                             uint32_t sendIndex, float gainLinear) = 0;

    /**
     * @param panPosition  0.0 (left) – 0.5 (centre) – 1.0 (right)
     */
    virtual void setSendPan(TrackID trackId, bool isPreFader,
                            uint32_t sendIndex, float panPosition) = 0;

    /**
     * @param enabled  false = bypass (signal blocked at the tap node)
     */
    virtual void setSendEnabled(TrackID trackId, bool isPreFader,
                                uint32_t sendIndex, bool enabled) = 0;

    /**
     * @brief Re-route this send to a different bus.
     *        Pushes NODE_DISCONNECT + NODE_CONNECT mutations; topology rebuilt
     *        asynchronously on the scheduler butler thread.
     * @param destinationNodeId  Target AUX/Bus NodeID; invalid() = disconnect only
     */
    virtual void setSendDestination(TrackID trackId, bool isPreFader,
                                    uint32_t sendIndex, NodeID destinationNodeId) = 0;

    // --- Virtual Instrument Controls ---
    /**
     * @brief Instantiates and inserts a virtual instrument plugin.
     */
    virtual void insertInstrument(TrackID trackId, uint32_t pluginId) = 0;

    /**
     * @brief Removes the virtual instrument from the slot.
     */
    virtual void removeInstrument(TrackID trackId) = 0;

    /**
     * @brief Toggles the bypass state of the virtual instrument slot.
     */
    virtual void setInstrumentBypassed(TrackID trackId, bool bypassed) = 0;

    /**
     * @brief Opens the virtual instrument editor inside a native parent window frame.
     */
    virtual bool openInstrumentEditor(TrackID trackId, void* parentWindow, int& outWidth, int& outHeight) = 0;

    /**
     * @brief Closes the virtual instrument editor.
     */
    virtual void closeInstrumentEditor(TrackID trackId) = 0;

    // --- Insert Effect Controls ---
    /**
     * @brief Instantiates and inserts a plugin into a slot.
     */
    virtual void insertPlugin(TrackID trackId, uint32_t slotIndex, uint32_t pluginId) = 0;

    /**
     * @brief Removes the plugin from the slot.
     */
    virtual void removePlugin(TrackID trackId, uint32_t slotIndex) = 0;

    /**
     * @brief Toggles the bypass state of a plugin slot.
     */
    virtual void setPluginBypassed(TrackID trackId, uint32_t slotIndex, bool bypassed) = 0;

    /**
     * @brief Retrieves the opaque binary state chunk (VST/AU preset) from a loaded plugin.
     */
    virtual std::vector<uint8_t> getPluginState(TrackID trackId, uint32_t slotIndex) const = 0;

    /**
     * @brief Injects an opaque binary state chunk into a loaded plugin.
     */
    virtual void setPluginState(TrackID trackId, uint32_t slotIndex, const std::vector<uint8_t>& state) = 0;

    /**
     * @brief Sets a parameter value on a loaded insert plugin (normalized 0.0 to 1.0).
     */
    virtual void setPluginParameter(TrackID trackId, uint32_t slotIndex, uint32_t paramIndex, float value) = 0;

    /**
     * @brief Retrieves the pluginId loaded in the specified slot, or UINT32_MAX if empty.
     */
    virtual uint32_t getPluginIdInSlot(TrackID trackId, uint32_t slotIndex) const = 0;

    /**
     * @brief Performs strict exact case-sensitive lookup for a plugin by name, returning pluginId or UINT32_MAX.
     */
    virtual uint32_t findPluginIdByName(std::string_view name) const = 0;

    /**
     * @brief Opens the plugin editor inside a native parent window frame.
     */
    virtual bool openPluginEditor(TrackID trackId, uint32_t slotIndex, void* parentWindow, int& outWidth, int& outHeight) = 0;

    /**
     * @brief Closes the plugin editor.
     */
    virtual void closePluginEditor(TrackID trackId, uint32_t slotIndex) = 0;

    using PluginParameterTweakedCallback = std::function<void(TrackID trackId, uint32_t slotIndex, const char* paramName, float value)>;
    virtual void subscribeToPluginParameterTweaks(TrackID trackId, uint32_t slotIndex, bool isInstrument, PluginParameterTweakedCallback cb) = 0;
    virtual void unsubscribeFromPluginParameterTweaks(TrackID trackId, uint32_t slotIndex, bool isInstrument) = 0;

    // --- Sidechain Control Facade ---
    virtual void setPluginSidechainSource(TrackID targetTrackId, uint32_t slotIndex, TrackID sourceTrackId, float sendGaindB = 0.0f) = 0;
    virtual void clearPluginSidechainSource(TrackID targetTrackId, uint32_t slotIndex) = 0;
    virtual std::vector<TrackInputOption> getAvailableSidechainSources(TrackID targetTrackId) const = 0;
    virtual SidechainSlotUIState getPluginSidechainState(TrackID targetTrackId, uint32_t slotIndex) const = 0;

    // --- State Queries ---
    virtual uint32_t getTrackCount() const = 0;
    virtual TrackUIState getTrackState(TrackID trackId) const = 0;
    virtual std::vector<TrackUIState> getAllTracks() const = 0;
    virtual TrackDynamicState getDynamicState(NodeID channelStripNode) const = 0;
    virtual std::vector<PluginDescriptor> getAvailablePlugins() const = 0;

    virtual void setTrackLocked(TrackID id, bool locked) = 0;
    virtual void setTrackSelected(TrackID id, bool selected) = 0;
    virtual void clearTrackSelection() = 0;
    virtual void setAutomationExpanded(TrackID id, bool expanded) = 0;
    virtual void setTakesExpanded(TrackID id, bool expanded) = 0;
    virtual void setAutomationSubLaneExpanded(TrackID id, uint32_t subLaneIndex, bool expanded) = 0;
    virtual void setAutomationSubLaneHeight(TrackID id, uint32_t subLaneIndex, uint32_t heightPx) = 0;

    using AutomationLaneRequestCallback = std::function<void(TrackID trackId, NodeID routingNodeId, uint32_t subNodeId, uint32_t parameterIndex)>;
    virtual void setAutomationLaneRequestCallback(AutomationLaneRequestCallback cb) = 0;
    virtual void requestAutomationLaneForLastTweaked(TrackID trackId) = 0;

    // --- Master Bus ---
    virtual NodeID getMasterChannelStripNode() const = 0;
};

} // namespace bridge
