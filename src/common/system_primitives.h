// system_primitives.h v1.2
// Last Updated: 2026-05-07
//
// This file defines the core POD (Plain Old Data) primitives that cross
// layer boundaries in the DAW architecture. All structures are true PODs
// with explicit data formats - no hidden allocations, no template magic.
//
// DESIGN PRINCIPLES:
// - All structures are Plain Old Data (POD)
// - No constructors, no destructors, no virtual methods
// - Safe to memcpy across boundaries
// - Explicit data formats (no hidden type information)
// - Fixed-size arrays (no dynamic allocation)
// - Compile-time POD verification with static_assert

#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <numbers>

//=== Forward declarations for cross-referenced types ===//
namespace Layer2 {
    class IMemoryCoordinator;
}

//=== Math Constants ===//
namespace MathConstants {
    constexpr float PI      = std::numbers::pi_v<float>;
    constexpr float HALF_PI = std::numbers::pi_v<float> * 0.5f;
    constexpr float TWO_PI  = std::numbers::pi_v<float> * 2.0f;
}

//=== Constants ===//
constexpr uint32_t MAX_CHANNELS = 32;
constexpr uint32_t MAX_NAME_LENGTH = 64;
constexpr uint32_t MAX_COMMENT_LENGTH = 1024;
constexpr uint32_t MAX_PATH_LENGTH = 512;
constexpr uint32_t MAX_PLUGIN_NAME_LENGTH = 128;

namespace SystemDefaults {
    // Parameter Default States (Normalized [0.0, 1.0])
    constexpr float VolumeNormalizedDefault = 0.70710678f; // 0.0 dB Unity Gain
    constexpr float PanNormalizedDefault    = 0.5f;        // Center Pan
    constexpr float MuteNormalizedDefault   = 0.0f;        // Unmuted
    constexpr float SoloNormalizedDefault   = 0.0f;        // Unsoloed
    constexpr float BypassNormalizedDefault = 0.0f;        // Unbypassed

    // Parameter Bounds
    constexpr float ParameterMinDefault = 0.0f;
    constexpr float ParameterMaxDefault = 1.0f;
}

//==============================================================================
// LAYER 1 PRIMITIVES (Hardware/OS Abstraction)
//==============================================================================

//=== Audio Buffer Primitive (Planar, Branch-Free) ===//

// RAII handle for buffer lifecycle management
class AudioBufferHandle {
  Layer2::IMemoryCoordinator *pool;
  struct AudioBuffer *buffer;

public:
  AudioBufferHandle();
  AudioBufferHandle(Layer2::IMemoryCoordinator *p, struct AudioBuffer *buf);

  ~AudioBufferHandle();

  // Move-only (no copying)
  AudioBufferHandle(const AudioBufferHandle &) = delete;
  AudioBufferHandle &operator=(const AudioBufferHandle &) = delete;
  AudioBufferHandle(AudioBufferHandle &&other) noexcept;
  AudioBufferHandle &operator=(AudioBufferHandle &&other) noexcept;

  // Access the buffer
  struct AudioBuffer &get();
  const struct AudioBuffer &get() const;
  struct AudioBuffer *operator->();
  const struct AudioBuffer *operator->() const;

  explicit operator bool() const;
  bool isValid() const;
};

// AudioBuffer definition (TRUE POD)
struct AudioBuffer {
  uint32_t numChannels; // Channel count (1-32)
  uint32_t numFrames;   // Frame count
  uint32_t bufferId;    // Opaque handle
  uint16_t flags;       // Buffer flags
  uint8_t reserved[10]; // Padding to 32 bytes

  // Channel data pointers (planar format)
  float *channelData[MAX_CHANNELS];

  // Buffer flags
  enum BufferFlags : uint16_t {
    NONE = 0x0000,
    SILENCE = 0x0001,
    CLIPPED = 0x0002,
    INTERLEAVED = 0x0004,
    READ_ONLY = 0x0008,
    TEMPORARY = 0x0010,
  };
};

// COMPILE-TIME ASSERTION: AudioBuffer must be POD
static_assert(std::is_pod<AudioBuffer>::value,
              "AudioBuffer must be Plain Old Data");

//==============================================================================
// NODE IDENTIFIER (v1.2)
//==============================================================================

/**
 * @brief Unified node identifier with generation counter
 * 
 * Memory Layout: 64-bit total (two 32-bit fields)
 * - id: [0, 2^16-1] (16-bit index limited to 65535 nodes)
 * - generation: [0, 2^16-1] (16-bit generation counter)
 */
struct NodeID {
    uint32_t id;           // Index into state registry [0, 65535]
    uint32_t generation;   // Generation counter for ABA protection
    
    // Validity check
    constexpr bool isValid() const {
        return id != UINT32_MAX && generation != 0;
    }
    
    // Static factory for invalid ID
    static constexpr NodeID invalid() {
        return {UINT32_MAX, 0};
    }
    
    // Equality comparison
    constexpr bool operator==(const NodeID& other) const {
        return id == other.id && generation == other.generation;
    }
    
    // Equality comparison
    constexpr bool operator!=(const NodeID& other) const {
        return !(*this == other);
    }
    
    // Conversion helper for Layer 4's internal 16-bit packing
    // Packs into uint32_t: [31:16] = generation, [15:0] = id
    constexpr uint32_t toPacked() const {
        return (generation << 16) | (id & 0xFFFF);
    }
    
    
    // Packs into uint64_t for map keys or storage
    constexpr uint64_t toRaw() const {
        return (static_cast<uint64_t>(generation) << 32) | id;
    }
    
    // Unpacks from uint64_t format
    static constexpr NodeID fromRaw(uint64_t raw) {
        return NodeID{static_cast<uint32_t>(raw & 0xFFFFFFFF), static_cast<uint32_t>(raw >> 32)};
    }
    
    // Direct field access helpers (for clarity in Layer 4 code)
    constexpr uint32_t index() const { return id; }
    constexpr uint32_t gen() const { return generation; }
};

// Compile-time assertions
static_assert(sizeof(NodeID) == 8, "NodeID must be exactly 8 bytes");
static_assert(std::is_pod<NodeID>::value, "NodeID must be Plain Old Data");

//==============================================================================
// EVENT PRIMITIVE (v1.2 - With Routing Support)
//==============================================================================

// Event data with explicit payload union (TRUE POD)
// Used for high-frequency, sample-accurate event delivery from automation
// and MIDI sources to DSP processing nodes via IEventQueue
struct EventData {
  // Event routing (v1.2)
  NodeID targetNodeId;   // Which node receives this event (for routing)

  // Event timing (sample-accurate)
  uint32_t sampleOffset; // Offset within current buffer (0 to bufferSize-1)

  // Event classification
  uint8_t eventType; // Event type enumeration
  uint8_t flags;     // Event flags (bit field)
  uint16_t padding;  // Reserved for future use

  // Explicit payload union (16 bytes max, fully documented)
  union Payload {
    // Automation events
    struct {
      uint32_t parameterIndex; // Parameter index (node-specific)
      float targetValue;       // Target value
      uint32_t rampDuration;   // For ramped automation (samples)
      uint32_t targetSubNodeId; // For nested node routing
    } automation;

    // MIDI Note events
    struct {
      uint8_t pitch;    // 0-127
      uint8_t velocity; // 0-127
      uint8_t channel;  // 0-15
      uint8_t reserved; // Reserved
    } midiNote;

    // MIDI CC events
    struct {
      uint8_t controllerNumber; // 0-127
      uint8_t value;            // 0-127
      uint8_t channel;          // 0-15
      uint8_t reserved;         // Reserved
    } midiCC;

    // MIDI Pitch Bend events
    struct {
      uint16_t value;     // 0-16383 (8192 = center)
      uint8_t channel;    // 0-15
      uint8_t reserved;   // Reserved
      uint32_t reserved2; // Reserved
    } midiPitch;

    // Transport events
    struct {
      uint8_t transportState; // STOPPED, PLAYING, RECORDING
      uint8_t reserved[3];    // Reserved
      uint32_t reserved2;     // Reserved
    } transport;

    // Raw byte array for custom events
    uint8_t raw[16]; // 16 bytes max
  } payload;
};

// Event type enumeration
namespace EventType {
constexpr uint8_t AUTOMATION      = 0;
constexpr uint8_t MIDI_NOTE_OFF   = 1;  // Priority 1: Clear/release synth voices first
constexpr uint8_t MIDI_NOTE_ON    = 2;  // Priority 2: Trigger new voices
constexpr uint8_t MIDI_CC         = 3;  // Priority 3: Parameter modulation
constexpr uint8_t MIDI_PITCH      = 4;  // Priority 4: Pitch bending
constexpr uint8_t TRANSPORT       = 5;
constexpr uint8_t METADATA        = 6;
constexpr uint8_t CUSTOM          = 255;
} // namespace EventType

// Layer 2: Deterministic Event Micro-Comparator (Strict Weak Ordering)
struct EventMicroComparator {
    inline bool operator()(const EventData& lhs, const EventData& rhs) const noexcept {
        // Primary sort: Ascending sample offset
        if (lhs.sampleOffset != rhs.sampleOffset) {
            return lhs.sampleOffset < rhs.sampleOffset;
        }

        // Secondary sort: Micro-timing priorities (lower eventType value = processed first)
        // Order: AUTOMATION -> NOTE_OFF -> NOTE_ON -> CC -> PITCH -> TRANSPORT
        return lhs.eventType < rhs.eventType;
    }
};

// RT Audio Thread safe event pre-sorting
inline void sort_events_in_place(EventData* events, uint32_t count) noexcept {
    if (count <= 1) return;
    
    // Insertion sort: Deterministic worst-case performance, zero heap allocations, O(1) space
    for (uint32_t i = 1; i < count; ++i) {
        EventData key = events[i];
        int32_t j = static_cast<int32_t>(i) - 1;
        EventMicroComparator comp;
        
        while (j >= 0 && comp(key, events[j])) {
            events[j + 1] = events[j];
            j--;
        }
        events[j + 1] = key;
    }
}

// COMPILE-TIME ASSERTION: EventData must be POD
static_assert(std::is_pod<EventData>::value,
              "EventData must be Plain Old Data");

//==============================================================================
// LAYER 2 PRIMITIVES (Core Infrastructure Services)
//==============================================================================

//=== System Mutation Primitive (TRUE POD) ===//

//=== Telemetry Frame Primitive (TRUE POD) ===//

struct TelemetryFrame {
  enum Type : uint8_t {
    PEAK_METER,
    RMS_METER,
    PLAYHEAD_POSITION,
    CPU_LOAD,
    PLAYBACK_STATE,
    TRACK_STATE,
    EVENT_OVERFLOW
  } type;

  uint8_t priority; // 0=critical, 255=background
  uint16_t reserved;

  uint32_t sourceId;   // Which track generated this
  uint64_t timestamp;  // When this frame was generated
  uint32_t payload[4]; // Type-specific data
};

// COMPILE-TIME ASSERTION: TelemetryFrame must be POD
static_assert(std::is_pod<TelemetryFrame>::value,
              "TelemetryFrame must be Plain Old Data");

//=== State Management Primitives ===//

struct StateSnapshotID {
  uint64_t id;         // Snapshot identifier
  uint32_t generation; // Generation counter (ABA protection)
  uint32_t reserved;   // Future use

  constexpr bool isValid() const { return id != UINT64_MAX && generation != 0; }

  constexpr static StateSnapshotID invalid() { return {UINT64_MAX, 0, 0}; }

  constexpr bool operator==(const StateSnapshotID &other) const {
    return id == other.id && generation == other.generation;
  }

  constexpr bool operator!=(const StateSnapshotID &other) const {
    return !(*this == other);
  }
};

// COMPILE-TIME ASSERTION: StateSnapshotID must be POD
static_assert(std::is_pod<StateSnapshotID>::value,
              "StateSnapshotID must be Plain Old Data");

struct StateDelta {
  StateSnapshotID previousId;
  StateSnapshotID newId;
  uint32_t deltaDataId;
  char description[64]; // Fixed-size (NOT dynamically allocated)
  uint64_t timestamp;
  uint32_t checksum;
  uint32_t reserved;
};

// COMPILE-TIME ASSERTION: StateDelta must be POD
static_assert(std::is_pod<StateDelta>::value,
              "StateDelta must be Plain Old Data");

//=== Plugin Discovery Primitives ===//

namespace PluginCategory {
constexpr uint8_t EFFECT_OTHER = 0;
constexpr uint8_t INSTRUMENT = 1;
constexpr uint8_t EFFECT_DELAY_REVERB = 2;
constexpr uint8_t EFFECT_DISTORTION = 3;
constexpr uint8_t EFFECT_DYNAMICS = 4;
constexpr uint8_t EFFECT_EQ_FILTER = 5;
constexpr uint8_t EFFECT_MODULATION = 6;
} // namespace PluginCategory


struct PluginDescriptor {
  uint32_t pluginId;
  char name[MAX_PLUGIN_NAME_LENGTH];         // Fixed-size (NOT pointer)
  char manufacturer[MAX_PLUGIN_NAME_LENGTH]; // Fixed-size (NOT pointer)
  char filePath[MAX_PATH_LENGTH];            // Fixed-size (NOT pointer)
  uint32_t version;     // Packed: (major << 16) | (minor << 8) | patch
  uint16_t formatFlags; // VST3, AU, CLAP, etc.
  uint8_t capabilities; // Instrument, hasEditor, etc.
  uint8_t category;     // Effect, Instrument, Analyzer, etc.
  uint64_t fileModTime; // File modification time
  uint32_t fileSize;    // File size in bytes
  uint32_t checksum;    // Plugin binary checksum
  uint32_t reserved;

  constexpr bool isInstrument() const { return capabilities & 0x01; }

  constexpr bool hasEditor() const { return capabilities & 0x02; }

};

// COMPILE-TIME ASSERTION: PluginDescriptor must be POD
static_assert(std::is_pod<PluginDescriptor>::value,
              "PluginDescriptor must be Plain Old Data");

//==============================================================================
// LAYER 3 PRIMITIVES (Core Audio Engine)
//==============================================================================

//=== DSP Node Primitive (v1.1 - Pure POD, No State Pointer) ===//

/**
 * @brief Unified node identifier with generation counter
 * 
 * This primitive is used consistently across ALL layers:
 * - Layer 3: Processing graph node references
 * - Layer 4: DSP node factory returns and processing function receives
 * - Layer 5: TrackPipelineDescriptor contains NodeID fields
 * - Layer 6: Export configuration specifies NodeID targets
 * - Layer 7: UI resolves TrackID to NodeID for export
 */

struct DSPNode {
  uint32_t type;     // Node type identifier
  NodeID id;         // Unified node ID
  uint32_t flags;    // Node flags
  uint32_t reserved; // Future use

  // Helper: Validation
  constexpr bool isValid() const { return id.isValid(); }
};

// COMPILE-TIME ASSERTION: DSPNode must be POD
static_assert(std::is_pod<DSPNode>::value, "DSPNode must be Plain Old Data");


//=== DSP Connection Primitive ===//

struct DSPConnection {
  uint32_t sourceNodeIndex; // Source node index
  uint32_t sourcePort;      // Source output port index
  uint32_t destNodeIndex;   // Destination node index
  uint32_t destPort;        // Destination input port index
  float gain;               // Connection gain (1.0 = unity)

  constexpr bool isValid() const {
    return gain >= 0.0f && sourceNodeIndex != UINT32_MAX &&
           destNodeIndex != UINT32_MAX;
  }

  constexpr bool operator==(const DSPConnection &other) const {
    return sourceNodeIndex == other.sourceNodeIndex &&
           sourcePort == other.sourcePort &&
           destNodeIndex == other.destNodeIndex && destPort == other.destPort;
  }
};

// COMPILE-TIME ASSERTION: DSPConnection must be POD
static_assert(std::is_pod<DSPConnection>::value,
              "DSPConnection must be Plain Old Data");

/// Warp behavior for a clip on an audio track.
/// Used by Layer 5 Arrangement and consumed by Layer 4 SoundTouchNode.
enum class WarpMode : uint8_t {
    BYPASS,           ///< Direct pass-through, no processing.
    SCRUB,            ///< SoundTouch: optimised for real-time scrubbing.
    SYNC_TO_TEMPO,    ///< Ratio = MediaBPM / ProjectBPM (auto-calculated by Layer 5).
    MANUAL,           ///< User-defined timeRatio via automation.
};

/// POD parameters for SoundTouchNode — safe to carry in SystemMutation::payload.
struct SoundTouchParams {
    WarpMode warpMode;          ///< Active warp mode
    uint8_t  reserved[3];
    float    timeRatio;         ///< 1.0 = original speed (MANUAL / SCRUB modes)
    float    pitchSemiTones;    ///< Semitone shift (-24 .. +24)
    float    mediaBPM;          ///< Source file BPM (set by Layer 6 analyzer)
};

static_assert(sizeof(SoundTouchParams) == 16, "SoundTouchParams must be 16 bytes");
static_assert(std::is_pod<SoundTouchParams>::value, "SoundTouchParams must be Plain Old Data");

struct MonitorPayload {
    uint8_t  monitorState; // cast from MonitorState or acceptLiveMIDI (0 or 1)
    uint8_t  reserved[3];
    uint32_t reserved2[7];
};

static_assert(sizeof(MonitorPayload) == 32, "MonitorPayload must be 32 bytes");
static_assert(std::is_pod<MonitorPayload>::value, "MonitorPayload must be Plain Old Data");

struct RecordPayload {
    bool isArmed;
    uint8_t reserved1[7];
    void* recordingQueue;
    uint8_t reserved2[16];
};

static_assert(sizeof(RecordPayload) == 32, "RecordPayload must be exactly 32 bytes");
static_assert(std::is_pod<RecordPayload>::value, "RecordPayload must be a POD type");

//=== Sidechain Primitives ===//

struct SidechainConnection {
    NodeID   sourceNodeId;       // Source track/node outputting audio (e.g. Kick Track output node)
    NodeID   destNodeId;         // Target DSP/Plugin node receiving sidechain signal
    uint32_t destSlotIndex;      // Effect slot index (0-7)
    uint32_t destInputIndex;     // Auxiliary input bus index (1 = Sidechain)
    float    gain;               // Sidechain send level (linear gain, default 1.0f)
    bool     isEnabled;          // Routing active flag
    uint8_t  padding[3];         // Struct byte alignment
};

static_assert(sizeof(SidechainConnection) == 32, "SidechainConnection must be exactly 32 bytes");
static_assert(std::is_standard_layout_v<SidechainConnection> && std::is_trivially_copyable_v<SidechainConnection>, 
              "SidechainConnection must satisfy C++20 trivial copy and standard layout requirements");

struct PlanarSidechainBuffer {
    float*   channels[MAX_CHANNELS]; // Planar channel pointers (e.g. stereo aux input)
    uint32_t numChannels;            // Channel count (typically 1 or 2)
    uint32_t numFrames;              // Frame count matching block size
};

//=== System Mutation Primitive (TRUE POD) ===//

struct SystemMutation {
  uint64_t sequenceNumber; // Global ordering
  uint8_t priority;        // 0=critical, 255=background
  uint8_t type;            // Mutation type enum
  uint16_t flags;          // Feature flags

  NodeID targetId;       // Target node (opaque handle)
  uint32_t dependencyId; // Must complete before this mutation

  // Compact payload (no strings, no pointers)
  union {
    uint32_t payload[8]; // 32 bytes of parameter data (backwards compatible)
    DSPNode node;        // For NODE_ADD
    DSPConnection connection; // For NODE_CONNECT/DISCONNECT
    SoundTouchParams soundTouch; // For SoundTouch updates
    MonitorPayload monitor;
    RecordPayload record;
    SidechainConnection sidechain; // For SIDECHAIN_CONNECT/DISCONNECT
  };

  // Helper methods (constexpr for compile-time evaluation)
  constexpr bool hasDependency() const { return dependencyId != 0; }
};

// COMPILE-TIME ASSERTION: SystemMutation must be POD
static_assert(std::is_pod<SystemMutation>::value,
              "SystemMutation must be Plain Old Data");


//=== Transport Primitives ===//

enum class TransportState : uint8_t { STOPPED, PLAYING, RECORDING };

enum class PlaybackMode : uint8_t { SONG, PATTERN };

struct TransportPosition {
  uint64_t positionSample; // Current position
  double bpm;              // Tempo at current position
  uint8_t numerator;       // Time signature numerator
  uint8_t denominator;     // Time signature denominator
  uint32_t bar;            // Current bar (1-based)
  uint32_t beat;           // Current beat (1-based)
  uint32_t tick;           // Current tick
  uint32_t ticksPerBeat;   // Ticks per quarter note (e.g., 960)
};

// COMPILE-TIME ASSERTION: TransportPosition must be POD
static_assert(std::is_pod<TransportPosition>::value,
              "TransportPosition must be Plain Old Data");

struct LoopState {
  enum class LoopMode : uint8_t { DISABLED, ENABLED, PUNCH_IN, PUNCH_OUT };

  LoopMode mode;
  uint64_t startSample;
  uint64_t endSample;
  uint32_t crossfadeSamples;

  constexpr bool isLooping() const { return mode == LoopMode::ENABLED; }


  constexpr bool isValid() const {
    return endSample > startSample &&
           (endSample - startSample) > crossfadeSamples;
  }
};

// COMPILE-TIME ASSERTION: LoopState must be POD
static_assert(std::is_pod<LoopState>::value,
              "LoopState must be Plain Old Data");

//=== Plugin Host Primitives ===//

enum class PluginFormat : uint8_t { NONE, VST3, AU, CLAP };

// Bit-flags for PluginDescriptor::formatFlags
// Each plugin binary may support multiple formats simultaneously.
namespace PluginFormatFlags {
constexpr uint16_t VST3 = 0x0001;
constexpr uint16_t AU = 0x0002;
constexpr uint16_t CLAP = 0x0004;
} // namespace PluginFormatFlags

struct PluginHandle {
  uint32_t id;
  uint32_t generation;
  PluginFormat format;

  constexpr bool isValid() const {
    return id != UINT32_MAX && generation != 0 && format != PluginFormat::NONE;
  }

  constexpr static PluginHandle invalid() {
    return {UINT32_MAX, 0, PluginFormat::NONE};
  }

  constexpr bool operator==(const PluginHandle &other) const {
    return id == other.id && generation == other.generation;
  }
};

// COMPILE-TIME ASSERTION: PluginHandle must be POD
static_assert(std::is_pod<PluginHandle>::value,
              "PluginHandle must be Plain Old Data");

//==============================================================================
// LAYER 4 PRIMITIVES (DSP Processing Nodes)
//==============================================================================

//=== Time Representation Primitives ===//

enum class TimeDomain : uint8_t { AUDIO_TIME, BEATS_TIME, BAR_TIME };

struct TimePosition {
  TimeDomain domain;

  union {
    uint64_t samples;
    double beats;
    struct {
      uint32_t bar;
      uint32_t beat;
      uint32_t tick;
      uint32_t reserved;
    } bbt;
  } position;

  constexpr TimePosition() : domain(TimeDomain::AUDIO_TIME), position{0} {}

  constexpr explicit TimePosition(uint64_t s) : domain(TimeDomain::AUDIO_TIME), position{s} {}

  constexpr explicit TimePosition(double b) : domain(TimeDomain::BEATS_TIME), position{0} {
    position.beats = b;
  }

};

// COMPILE-TIME ASSERTION: TimePosition must be trivially copyable
static_assert(std::is_trivially_copyable<TimePosition>::value,
              "TimePosition must be trivially copyable");

//==============================================================================
// ADDITIONAL PRIMITIVES
//==============================================================================

//=== Type-Safe Opaque Handles ===//

template <typename T, typename Tag = void> struct TypedHandle {
  uint32_t id;
  uint32_t generation;

  constexpr bool isValid() const { return id != UINT32_MAX && generation != 0; }

  constexpr static TypedHandle invalid() { return {UINT32_MAX, 0}; }

  constexpr bool operator==(const TypedHandle &other) const {
    return id == other.id && generation == other.generation;
  }

  constexpr uint64_t toRaw() const {
    return (static_cast<uint64_t>(generation) << 32) | id;
  }

  static constexpr TypedHandle fromRaw(uint64_t raw) {
    return {static_cast<uint32_t>(raw & 0xFFFFFFFF), static_cast<uint32_t>(raw >> 32)};
  }
};

struct ArrangementID {
    uint32_t id;
    
    static constexpr ArrangementID invalid() { return { 0xFFFFFFFFu }; }
    bool isValid() const { return id != 0xFFFFFFFFu; }
    bool operator==(const ArrangementID& o) const { return id == o.id; }
    bool operator!=(const ArrangementID& o) const { return id != o.id; }
};

static_assert(std::is_pod<ArrangementID>::value, "ArrangementID must be Plain Old Data");

struct MarkerUUID {
    uint8_t bytes[16];

    bool operator==(const MarkerUUID& other) const {
        return std::memcmp(bytes, other.bytes, 16) == 0;
    }
    bool operator!=(const MarkerUUID& other) const {
        return !(*this == other);
    }
    bool isZero() const {
        for (int i = 0; i < 16; ++i) {
            if (bytes[i] != 0) return false;
        }
        return true;
    }
};

static_assert(std::is_pod<MarkerUUID>::value, "MarkerUUID must be Plain Old Data");


struct ArrangementInfo {
    ArrangementID id;
    char name[MAX_NAME_LENGTH];
    bool isActive;
    uint8_t padding[3]; // Structural alignment padding
};

static_assert(std::is_pod<ArrangementInfo>::value, "ArrangementInfo must be Plain Old Data");

struct MergeFilterOptions {
    bool importAudio;
    bool importMIDI;
    bool importAutomation;
    bool importMixerSettings;
    bool limitToLoopRange;
    uint64_t loopStartFrame;
    uint64_t loopEndFrame;
};

static_assert(std::is_pod<MergeFilterOptions>::value, "MergeFilterOptions must be Plain Old Data");

enum class RenderFormat : uint8_t {
    WAV = 0,
    FLAC = 1,
    MP3 = 2
};

struct RenderConfiguration {
    char outputFilePath[MAX_PATH_LENGTH];
    RenderFormat format;
    uint8_t bitDepth;          // 16, 24, or 32
    bool enableDither;         // Inject TPDF dither
    bool splitPlanar;          // Export L/R files separately
    uint32_t sampleRate;
    uint64_t startFrame;
    uint64_t endFrame;
};

static_assert(std::is_pod<RenderConfiguration>::value, "RenderConfiguration must be Plain Old Data");

namespace std {
template <typename T, typename Tag>
struct hash<TypedHandle<T, Tag>> {
  std::size_t operator()(const TypedHandle<T, Tag>& handle) const noexcept {
    return std::hash<uint64_t>{}(handle.toRaw());
  }
};

template <>
struct hash<ArrangementID> {
  std::size_t operator()(const ArrangementID& handle) const noexcept {
    return std::hash<uint32_t>{}(handle.id);
  }
};

template <>
struct hash<MarkerUUID> {
  std::size_t operator()(const MarkerUUID& uuid) const noexcept {
    uint64_t high, low;
    std::memcpy(&high, uuid.bytes, 8);
    std::memcpy(&low, uuid.bytes + 8, 8);
    return std::hash<uint64_t>{}(high) ^ (std::hash<uint64_t>{}(low) << 1);
  }
};
}

using TrackID = TypedHandle<struct TrackTag>;
using BusID = TypedHandle<struct BusTag>;
using MediaID = TypedHandle<struct MediaTag>;
using ClipID = TypedHandle<struct ClipTag>;
using RegionID = TypedHandle<struct RegionTag>;

struct SidechainSlotUIState {
    bool     hasSidechainInput;              // True if hosted plugin has sidechain input bus
    bool     isConnected;                    // True if a source track is currently routed
    TrackID  sourceTrackId;                  // Connected source track ID
    float    sendGaindB;                     // Sidechain send gain in dB
    char     sourceTrackName[MAX_NAME_LENGTH];// Label of connected source track (e.g. "Kick")
};

static_assert(std::is_standard_layout_v<SidechainSlotUIState> && std::is_trivially_copyable_v<SidechainSlotUIState>, 
              "SidechainSlotUIState must satisfy C++20 trivial copy and standard layout requirements");

enum class RegionType : uint8_t {
    AUDIO = 0,
    MIDI = 1,
    AUTOMATION = 2
};

struct SnapshotRegion {
    TrackID trackId;            // 8 bytes
    RegionID regionId;          // 8 bytes
    RegionType type;            // 1 byte
    WarpMode warpMode;          // 1 byte
    uint8_t padding1[2];        // 2 bytes (padding to align uint32)
    uint32_t sourceId;          // 4 bytes
    uint64_t positionSample;    // 8 bytes
    uint64_t sourceStartSample; // 8 bytes
    uint64_t durationProjectFrames; // 8 bytes (length in project timeline frames)
    float gain;                 // 4 bytes
    float playbackRatio;        // 4 bytes
    float sourceBpm;            // 4 bytes
    uint32_t fadeInSamples;     // 4 bytes
    uint32_t fadeOutSamples;    // 4 bytes
    uint32_t sourceSampleRate;  // 4 bytes
    bool isMuted;               // 1 byte
    uint8_t padding2[7];        // 7 bytes padding (total struct size aligns to 8-byte boundaries)
};

static_assert(std::is_pod<SnapshotRegion>::value, "SnapshotRegion must be Plain Old Data");

constexpr uint32_t MAX_SNAPSHOT_REGIONS = 4096;
constexpr uint32_t MAX_BUFFERS_PER_TRACK = 4;

struct TimelineSnapshot {
    SnapshotRegion regions[MAX_SNAPSHOT_REGIONS];
    uint32_t regionCount;
};

static_assert(std::is_pod<TimelineSnapshot>::value, "TimelineSnapshot must be Plain Old Data");

//=== Musical Position ===//

struct MusicalPosition {
  uint64_t absoluteSample;
  double bpm;
  uint8_t numerator;
  uint8_t denominator;
  uint32_t bar;
  uint32_t beat;
  uint32_t tick;
  uint16_t ticksPerBeat;
};

// COMPILE-TIME ASSERTION: MusicalPosition must be POD
static_assert(std::is_pod<MusicalPosition>::value,
              "MusicalPosition must be Plain Old Data");

//=== SoundTouch Warp Primitives (v1.3) ===//



//=== MIDI Note Primitive ===//

struct MIDINote {
  uint64_t noteId;
  uint8_t pitch;
  uint8_t velocity;
  uint64_t offsetSample;
  uint64_t durationSample;
  uint8_t channel;
};

// COMPILE-TIME ASSERTION: MIDINote must be POD
static_assert(std::is_pod<MIDINote>::value, "MIDINote must be Plain Old Data");

struct MIDICCPoint {
  uint64_t absoluteTickPosition; // Absolute musical position (ticks from project start)
  uint64_t samplePosition;     // Relative to clip start
  uint8_t channel;
  uint8_t controllerNumber;
  uint8_t value;
  uint8_t padding[5];
};

static_assert(std::is_pod<MIDICCPoint>::value, "MIDICCPoint must be Plain Old Data");

struct MIDIPitchPoint {
  uint64_t absoluteTickPosition; // Absolute musical position (ticks from project start)
  uint64_t samplePosition;     // Relative to clip start
  uint8_t channel;
  uint16_t value;
  uint8_t padding[5];
};

static_assert(std::is_pod<MIDIPitchPoint>::value, "MIDIPitchPoint must be Plain Old Data");

class IMidiClipDataProvider {
public:
  virtual ~IMidiClipDataProvider() = default;
  virtual uint32_t getNotesInClip(ClipID clipId, MIDINote* outNotes, uint32_t maxNotes) const = 0;
  virtual uint32_t getCCPointsInClip(ClipID clipId, MIDICCPoint* outPoints, uint32_t maxPoints) const = 0;
  virtual uint32_t getPitchPointsInClip(ClipID clipId, MIDIPitchPoint* outPoints, uint32_t maxPoints) const = 0;
};


//=== Track Macro-Node Structural Constants & Parameter Primitives ===//
constexpr uint32_t MAX_TRACK_SENDS = 4;
constexpr uint32_t TRACK_MAIN_OUTPUT_CHANNELS = 2;
constexpr uint32_t TRACK_TOTAL_OUTPUT_PORTS = TRACK_MAIN_OUTPUT_CHANNELS + (MAX_TRACK_SENDS * 2); // 10 channels total
constexpr uint32_t TRACK_INPUT_HARDWARE_PORT_BASE = 0; // Inputs 0..1 (Live Hardware Input)
constexpr uint32_t TRACK_INPUT_PLAYBACK_PORT_BASE = 2; // Inputs 2..3 (Clip Playback Audio)

#include "common/math/smoothing.h"

enum class TrackMacroParameter : uint32_t {
    Volume           = 0,
    Pan              = 1,
    Mute             = 2,
    Solo             = 3,
    MonitorState     = 4,
    InputSourceIndex = 5,
    
    // Sends (4 Sends max: Gain, Pan, Pre/Post toggle)
    Send0Gain        = 10, Send0Pan = 11, Send0PrePost = 12,
    Send1Gain        = 13, Send1Pan = 14, Send1PrePost = 15,
    Send2Gain        = 16, Send2Pan = 17, Send2PrePost = 18,
    Send3Gain        = 19, Send3Pan = 20, Send3PrePost = 21,

    // Insert Plugin Bypasses & Sub-Parameter Ranges
    PluginBypassBase = 100 // Slot 0..7 bypasses at 100..107
};

struct MacroSendState {
    std::atomic<float> targetGain{0.0f};
    std::atomic<float> targetPan{0.5f};
    bool isPreFader{false};
    Math::ParameterSmoother gainSmoother;
    Math::ParameterSmoother panSmoother;

    void reset(float sampleRate) {
        targetGain.store(0.0f, std::memory_order_relaxed);
        targetPan.store(0.5f, std::memory_order_relaxed);
        isPreFader = false;
        gainSmoother.init(0.0f, 10.0f, sampleRate);
        panSmoother.init(0.5f, 10.0f, sampleRate);
    }
};

//=== Channel Strip State ===//

struct ChannelStripState {
  float gain;
  bool muted;
  bool soloed;
  bool phaseInverted;
  float stereoWidth;
};

// COMPILE-TIME ASSERTION: ChannelStripState must be POD
static_assert(std::is_pod<ChannelStripState>::value,
              "ChannelStripState must be Plain Old Data");

//=== Automation Point ===//

struct AutomationPoint {
  uint64_t positionSample;
  float value;
  enum class Shape : uint8_t {
    LINEAR,
    EXPONENTIAL,
    EASE_IN,
    EASE_OUT,
    EASE_IN_OUT,
    SINE,
    SQUARE,
    STEP
  } curveShape;
  float tension;
};

// COMPILE-TIME ASSERTION: AutomationPoint must be POD
static_assert(std::is_pod<AutomationPoint>::value,
              "AutomationPoint must be Plain Old Data");

//=== Automation Mode, Recorder State & Capture Point ===//

enum class AutomationMode : uint8_t {
  OFF = 0,
  READ,
  TOUCH,
  LATCH,
  WRITE,
  TRIM
};

struct RecorderState {
  NodeID targetNodeId;
  uint32_t parameterIndex;
  AutomationMode mode;
  bool isRecording;
  uint64_t startSample;
};

static_assert(std::is_pod<RecorderState>::value, "RecorderState must be Plain Old Data");

struct CapturePoint {
  NodeID targetId;           ///< Which node was changed
  uint32_t parameterIndex;   ///< Which parameter was changed
  uint64_t timestamp;        ///< Sample-accurate timestamp
  float value;               ///< New parameter value
  uint8_t flags;             ///< Capture flags (e.g. Touch/Release)
  uint8_t reserved[7];       ///< Padding
  
  // Flag bit definitions
  static constexpr uint8_t FLAG_TOUCH = 1 << 0;
  static constexpr uint8_t FLAG_RELEASE = 1 << 1;
};

static_assert(std::is_pod<CapturePoint>::value, "CapturePoint must be Plain Old Data");


//==============================================================================
// V1.3 PRIMITIVES (Plugin & Context Support)
//==============================================================================

//=== Parameter Metadata (v1.3) ===//

struct ParameterInfo {
    uint32_t index;
    char name[MAX_NAME_LENGTH];
    char unit[16];
    float minValue;
    float maxValue;
    float defaultValue;
    
    enum Flags : uint32_t {
        NONE = 0,
        IS_AUTOMATABLE = 1 << 0,
        IS_READ_ONLY = 1 << 1,
        IS_BOOLEAN = 1 << 2,
        IS_INTEGER = 1 << 3,
        IS_LOGARITHMIC = 1 << 4,
        IS_HIDDEN = 1 << 5,
        IS_MODULATABLE = 1 << 6,
    } flags;
};

// COMPILE-TIME ASSERTION: ParameterInfo must be POD
static_assert(std::is_pod<ParameterInfo>::value,
              "ParameterInfo must be Plain Old Data");

constexpr uint32_t BYPASS_PARAMETER_INDEX = 0xFFFF;

//=== Monitoring Primitives (v1.3) ===//
enum class MonitorState  : uint8_t { SILENCE = 0, INPUT = 1, DISK = 2 };

//=== Process Context (v1.3) ===//

struct ProcessContext {
    uint64_t hardwareTimestamp; // System-wide high-res time at start of cycle
    uint64_t cycleId;           // Monotonic counter for current audio cycle
    TransportPosition transport;
    TransportState transportState;
    float sampleRate;
    uint32_t maxBlockSize;
    uint32_t currentBlockSize;
    bool isOffline;
    NodeID isolateNodeId; // ID of the node to isolate for stem export (invalid = master mix)
    const float* const* inputChannels;    // Pointer to hardware capture buffers (RT-safe)
    uint32_t            numInputChannels; // Number of capture channels available
    const TimelineSnapshot* timelineSnapshot;
    const IMidiClipDataProvider* midiClipDataProvider;
    bool loopEnabled;
    uint64_t loopStart;
    uint64_t loopEnd;
    bool metronomeEnabled;
    void* sidechainManager;
};

// COMPILE-TIME ASSERTION: ProcessContext must be POD
static_assert(std::is_pod<ProcessContext>::value,
              "ProcessContext must be Plain Old Data");

//=== DSP Processing Function Signature (v1.3) ===//

// Processing function signature (called from audio thread)
// Parameters passed as planar float arrays for SIMD compatibility
// Events passed for sample-accurate timing and MIDI processing
// NOTE: Processing module maintains its own state lookup keyed by nodeId
using DSPProcessFunc = void(*)(
    NodeID nodeId,              // Unified NodeID handle
    float* const* inputs,        // Planar input arrays [channel][sample]
    float* const* outputs,       // Planar output arrays
    uint32_t numChannels,        // Number of channels
    uint32_t numSamples,         // Buffer size
    const EventData* events,     // Sample-accurate events
    uint32_t numEvents,          // Number of events
    EventData* outEvents,        // Buffer for output events
    uint32_t* outEventCount,     // Pointer to current output event count
    const ProcessContext* context, // Host process context (v1.3)
    const bool* inputSilence,     // Input silence flags (planar)
    bool* isOutputSilent          // Output silence flag reporting
);

//==============================================================================
// END OF SYSTEM_PRIMITIVES v1.3
//==============================================================================
