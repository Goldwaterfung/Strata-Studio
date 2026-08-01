// system_primitives.h v1.1
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

#include <cstdint>
#include <cstring>
#include <atomic>

//=== Forward declarations for cross-referenced types ===//
class IMemoryCoordinator;

//=== Constants ===//
constexpr uint32_t MAX_CHANNELS = 32;
constexpr uint32_t MAX_NAME_LENGTH = 64;
constexpr uint32_t MAX_PATH_LENGTH = 512;
constexpr uint32_t MAX_PLUGIN_NAME_LENGTH = 128;

//==============================================================================
// LAYER 1 PRIMITIVES (Hardware/OS Abstraction)
//==============================================================================

//=== Audio Buffer Primitive (Planar, Branch-Free) ===//

// RAII handle for buffer lifecycle management
class AudioBufferHandle {
    IMemoryCoordinator* pool;
    struct AudioBuffer* buffer;

public:
    AudioBufferHandle();
    AudioBufferHandle(IMemoryCoordinator* p, struct AudioBuffer* buf);

    ~AudioBufferHandle();

    // Move-only (no copying)
    AudioBufferHandle(const AudioBufferHandle&) = delete;
    AudioBufferHandle& operator=(const AudioBufferHandle&) = delete;
    AudioBufferHandle(AudioBufferHandle&& other) noexcept;
    AudioBufferHandle& operator=(AudioBufferHandle&& other) noexcept;

    // Access the buffer
    struct AudioBuffer& get();
    const struct AudioBuffer& get() const;
    struct AudioBuffer* operator->();
    const struct AudioBuffer* operator->() const;

    explicit operator bool() const;
    bool isValid() const;
};

// AudioBuffer definition (TRUE POD)
struct AudioBuffer {
    uint32_t numChannels;          // Channel count (1-32)
    uint32_t numFrames;            // Frame count
    uint32_t bufferId;             // Opaque handle
    uint16_t flags;                // Buffer flags
    uint8_t reserved[10];          // Padding to 32 bytes

    // Channel data pointers (planar format)
    float* channelData[MAX_CHANNELS];

    // Buffer flags
    enum BufferFlags : uint16_t {
        NONE           = 0x0000,
        SILENCE        = 0x0001,
        CLIPPED        = 0x0002,
        INTERLEAVED    = 0x0004,
        READ_ONLY      = 0x0008,
        TEMPORARY      = 0x0010,
    };
};

// COMPILE-TIME ASSERTION: AudioBuffer must be POD
static_assert(std::is_pod<AudioBuffer>::value,
              "AudioBuffer must be Plain Old Data");

//==============================================================================
// EVENT PRIMITIVE (v1.1 - Explicit Union Format)
//==============================================================================

// Event data with explicit payload union (TRUE POD)
struct EventData {
    // Event timing (sample-accurate)
    uint32_t sampleOffset;          // Offset within current buffer

    // Event classification
    uint8_t eventType;              // Event type enumeration
    uint8_t flags;                  // Event flags (bit field)
    uint16_t padding;               // Reserved for future use

    // Explicit payload union (16 bytes max, fully documented)
    union Payload {
        // Automation events
        struct {
            uint32_t parameterIndex;  // Parameter index (node-specific)
            float targetValue;        // Target value
            uint32_t rampDuration;    // For ramped automation (samples)
            uint32_t reserved;        // Reserved
        } automation;

        // MIDI Note events
        struct {
            uint8_t pitch;            // 0-127
            uint8_t velocity;         // 0-127
            uint8_t channel;          // 0-15
            uint8_t reserved;         // Reserved
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
            uint16_t value;           // 0-16383 (8192 = center)
            uint8_t channel;          // 0-15
            uint8_t reserved;         // Reserved
            uint32_t reserved2;       // Reserved
        } midiPitch;

        // Transport events
        struct {
            uint8_t transportState;   // STOPPED, PLAYING, RECORDING
            uint8_t reserved[3];      // Reserved
            uint32_t reserved2;       // Reserved
        } transport;

        // Raw byte array for custom events
        uint8_t raw[16];             // 16 bytes max
    } payload;
};

// Event type enumeration
namespace EventType {
    constexpr uint8_t AUTOMATION = 0;
    constexpr uint8_t MIDI_NOTE = 1;
    constexpr uint8_t MIDI_CC = 2;
    constexpr uint8_t MIDI_PITCH = 3;
    constexpr uint8_t TRANSPORT = 4;
    constexpr uint8_t METADATA = 5;
    constexpr uint8_t CUSTOM = 255;
}

// COMPILE-TIME ASSERTION: EventData must be POD
static_assert(std::is_pod<EventData>::value,
              "EventData must be Plain Old Data");

//==============================================================================
// LAYER 2 PRIMITIVES (Core Infrastructure Services)
//==============================================================================

//=== System Mutation Primitive (TRUE POD) ===//

struct SystemMutation {
    uint64_t sequenceNumber;    // Global ordering
    uint8_t priority;           // 0=critical, 255=background
    uint8_t type;               // Mutation type enum
    uint16_t flags;             // Feature flags

    uint32_t targetId;          // Target node (opaque handle)
    uint32_t dependencyId;      // Must complete before this mutation

    // Compact payload (no strings, no pointers)
    uint32_t payload[4];        // 16 bytes of parameter data

    // Helper methods (constexpr for compile-time evaluation)
    constexpr bool hasDependency() const {
        return dependencyId != UINT32_MAX;
    }
};

// COMPILE-TIME ASSERTION: SystemMutation must be POD
static_assert(std::is_pod<SystemMutation>::value,
              "SystemMutation must be Plain Old Data");

//=== Telemetry Frame Primitive (TRUE POD) ===//

struct TelemetryFrame {
    enum Type : uint8_t {
        PEAK_METER,
        RMS_METER,
        PLAYHEAD_POSITION,
        CPU_LOAD,
        PLAYBACK_STATE,
        TRACK_STATE
    } type;

    uint8_t priority;           // 0=critical, 255=background
    uint16_t reserved;

    uint32_t sourceId;          // Which track generated this
    uint64_t timestamp;         // When this frame was generated
    uint32_t payload[4];        // Type-specific data
};

// COMPILE-TIME ASSERTION: TelemetryFrame must be POD
static_assert(std::is_pod<TelemetryFrame>::value,
              "TelemetryFrame must be Plain Old Data");

//=== State Management Primitives ===//

struct StateSnapshotID {
    uint64_t id;               // Snapshot identifier
    uint32_t generation;       // Generation counter (ABA protection)
    uint32_t reserved;         // Future use

    constexpr bool isValid() const {
        return id != UINT64_MAX && generation != 0;
    }

    constexpr static StateSnapshotID invalid() {
        return {UINT64_MAX, 0, 0};
    }

    constexpr bool operator==(const StateSnapshotID& other) const {
        return id == other.id && generation == other.generation;
    }

    constexpr bool operator!=(const StateSnapshotID& other) const {
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
    char description[64];       // Fixed-size (NOT dynamically allocated)
    uint64_t timestamp;
    uint32_t checksum;
    uint32_t reserved;
};

// COMPILE-TIME ASSERTION: StateDelta must be POD
static_assert(std::is_pod<StateDelta>::value,
              "StateDelta must be Plain Old Data");

//=== Plugin Discovery Primitives ===//

struct PluginDescriptor {
    uint32_t pluginId;
    char name[MAX_PLUGIN_NAME_LENGTH];          // Fixed-size (NOT pointer)
    char manufacturer[MAX_PLUGIN_NAME_LENGTH];  // Fixed-size (NOT pointer)
    char filePath[MAX_PATH_LENGTH];            // Fixed-size (NOT pointer)
    uint32_t version;               // Packed: (major << 16) | (minor << 8) | patch
    uint16_t formatFlags;           // VST3, AU, CLAP, etc.
    uint8_t capabilities;           // Instrument, hasEditor, etc.
    uint8_t category;               // Effect, Instrument, Analyzer, etc.
    uint64_t fileModTime;           // File modification time
    uint32_t fileSize;              // File size in bytes
    uint32_t checksum;              // Plugin binary checksum
    uint32_t reserved;

    constexpr bool isInstrument() const {
        return capabilities & 0x01;
    }

    constexpr bool hasEditor() const {
        return capabilities & 0x02;
    }

    constexpr bool supportsFormat(uint16_t format) const {
        return formatFlags & format;
    }
};

// COMPILE-TIME ASSERTION: PluginDescriptor must be POD
static_assert(std::is_pod<PluginDescriptor>::value,
              "PluginDescriptor must be Plain Old Data");

//==============================================================================
// LAYER 3 PRIMITIVES (Core Audio Engine)
//==============================================================================

//=== DSP Node Primitive (v1.1 - Pure POD, No State Pointer) ===//

struct DSPNode {
    uint32_t type;              // Node type identifier
    uint32_t id;                // Opaque node ID (contains generation counter)
    uint32_t flags;             // Node flags
    uint32_t reserved;          // Future use

    // Helper: Extract generation counter (ABA protection)
    constexpr uint32_t getGeneration() const {
        return id >> 16;
    }

    // Helper: Extract array index (O(1) lookup)
    constexpr uint32_t getIndex() const {
        return id & 0xFFFF;
    }

    // Helper: Validation
    constexpr bool isValid() const {
        return getGeneration() != 0;
    }
};

// COMPILE-TIME ASSERTION: DSPNode must be POD
static_assert(std::is_pod<DSPNode>::value,
              "DSPNode must be Plain Old Data");

//=== DSP Connection Primitive ===//

struct DSPConnection {
    uint32_t sourceNodeIndex;   // Source node index
    uint32_t sourcePort;        // Source output port index
    uint32_t destNodeIndex;     // Destination node index
    uint32_t destPort;          // Destination input port index
    float gain;                 // Connection gain (1.0 = unity)

    constexpr bool isValid() const {
        return gain >= 0.0f &&
               sourceNodeIndex != UINT32_MAX &&
               destNodeIndex != UINT32_MAX;
    }

    constexpr bool operator==(const DSPConnection& other) const {
        return sourceNodeIndex == other.sourceNodeIndex &&
               sourcePort == other.sourcePort &&
               destNodeIndex == other.destNodeIndex &&
               destPort == other.destPort;
    }
};

// COMPILE-TIME ASSERTION: DSPConnection must be POD
static_assert(std::is_pod<DSPConnection>::value,
              "DSPConnection must be Plain Old Data");

//=== DSP Processing Function Signature (v1.1) ===//

// Processing function signature (called from audio thread)
// Parameters passed as planar float arrays for SIMD compatibility
// Events passed for sample-accurate timing and MIDI processing
// NOTE: Processing module maintains its own state lookup keyed by nodeId
using DSPProcessFunc = void(*)(
    uint32_t nodeId,            // Opaque node ID (module looks up its own state)
    float* const* inputs,        // Planar input arrays [channel][sample]
    float* const* outputs,       // Planar output arrays
    uint32_t numChannels,        // Number of channels
    uint32_t numSamples,         // Buffer size
    const EventData* events,     // Sample-accurate events
    uint32_t numEvents           // Number of events
);

//=== Transport Primitives ===//

enum class TransportState : uint8_t {
    STOPPED,
    PLAYING,
    RECORDING
};

struct TransportPosition {
    uint64_t positionSample;    // Current position
    double bpm;                 // Tempo at current position
    uint8_t numerator;          // Time signature numerator
    uint8_t denominator;        // Time signature denominator
    uint32_t bar;               // Current bar (1-based)
    uint32_t beat;              // Current beat (1-based)
    uint32_t tick;              // Current tick
};

// COMPILE-TIME ASSERTION: TransportPosition must be POD
static_assert(std::is_pod<TransportPosition>::value,
              "TransportPosition must be Plain Old Data");

struct LoopState {
    enum class LoopMode : uint8_t {
        DISABLED,
        ENABLED,
        PUNCH_IN,
        PUNCH_OUT
    };

    LoopMode mode;
    uint64_t startSample;
    uint64_t endSample;
    uint32_t crossfadeSamples;

    constexpr bool isLooping() const {
        return mode == LoopMode::ENABLED;
    }

    constexpr bool isPunching() const {
        return mode == LoopMode::PUNCH_IN || mode == LoopMode::PUNCH_OUT;
    }

    constexpr bool isValid() const {
        return endSample > startSample &&
               (endSample - startSample) > crossfadeSamples;
    }
};

// COMPILE-TIME ASSERTION: LoopState must be POD
static_assert(std::is_pod<LoopState>::value,
              "LoopState must be Plain Old Data");

//=== Plugin Host Primitives ===//

enum class PluginFormat : uint8_t {
    NONE,
    VST3,
    AU,
    CLAP
};

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

    constexpr bool operator==(const PluginHandle& other) const {
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

enum class TimeDomain : uint8_t {
    AUDIO_TIME,
    BEATS_TIME,
    BAR_TIME
};

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

    constexpr TimePosition() : domain(TimeDomain::AUDIO_TIME), position{} {}

    constexpr explicit TimePosition(uint64_t s)
        : domain(TimeDomain::AUDIO_TIME) {
        position.samples = s;
    }

    constexpr explicit TimePosition(double b)
        : domain(TimeDomain::BEATS_TIME) {
        position.beats = b;
    }

    constexpr uint64_t toSamples() const {
        return (domain == TimeDomain::AUDIO_TIME) ? position.samples : 0;
    }

    constexpr double toBeats() const {
        return (domain == TimeDomain::BEATS_TIME) ? position.beats : 0.0;
    }
};

// COMPILE-TIME ASSERTION: TimePosition must be trivially copyable
static_assert(std::is_trivially_copyable<TimePosition>::value,
              "TimePosition must be trivially copyable");

//==============================================================================
// ADDITIONAL PRIMITIVES
//==============================================================================

//=== Type-Safe Opaque Handles ===//

template<typename T, typename Tag = void>
struct TypedHandle {
    uint32_t id;
    uint32_t generation;

    constexpr bool isValid() const {
        return id != UINT32_MAX && generation != 0;
    }

    constexpr static TypedHandle invalid() {
        return {UINT32_MAX, 0};
    }

    constexpr bool operator==(const TypedHandle& other) const {
        return id == other.id && generation == other.generation;
    }
};

using TrackID = TypedHandle<struct TrackTag>;
using BusID = TypedHandle<struct BusTag>;
using MediaID = TypedHandle<struct MediaTag>;

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

//=== Audio Region Primitive ===//

struct AudioRegion {
    MediaID mediaId;
    uint64_t sourceStartSample;
    uint64_t sourceLength;
    uint64_t positionSample;
    double timeStretchRatio;
    double pitchShiftSemitones;
    float gainDecibels;
    uint32_t fadeInSamples;
    uint32_t fadeOutSamples;
    uint32_t flags;
};

// COMPILE-TIME ASSERTION: AudioRegion must be POD
static_assert(std::is_pod<AudioRegion>::value,
              "AudioRegion must be Plain Old Data");

//=== MIDI Note Primitive ===//

struct MIDINote {
    uint64_t noteId;
    uint8_t pitch;
    uint8_t velocity;
    uint64_t startSample;
    uint64_t endSample;
    uint8_t channel;
};

// COMPILE-TIME ASSERTION: MIDINote must be POD
static_assert(std::is_pod<MIDINote>::value,
              "MIDINote must be Plain Old Data");

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
        SQUARE
    } curveShape;
    float tension;
};

// COMPILE-TIME ASSERTION: AutomationPoint must be POD
static_assert(std::is_pod<AutomationPoint>::value,
              "AutomationPoint must be Plain Old Data");

//==============================================================================
// END OF SYSTEM_PRIMITIVES v1.1
//==============================================================================
