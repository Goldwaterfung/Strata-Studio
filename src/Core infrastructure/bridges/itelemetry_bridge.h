// itelemetry_bridge.h v1.0
// Telemetry Bridge Interface - Audio to GUI Thread Communication
//
// PURPOSE:
// - Provides lock-free telemetry queue from Audio (producers) to GUI (consumer)
// - Enables source filtering for efficient selective updates
// - Supports prioritized telemetry delivery
// - Optimized for high-frequency updates (metering, playhead)
//
// THREAD SAFETY:
// - push(): Called from multiple Audio/Worker threads (multiple producers)
// - poll(): Called from GUI/Main thread (single consumer)
// - Thread-safe when used by multiple producers and one consumer
//
// REAL-TIME GUARANTEES:
// - push() is wait-free and RT-safe (no allocations, no blocking)
// - poll() is wait-free (non-blocking)

#pragma once

#include "system_primitives.h"
#include <cstdint>
#include <memory>

namespace Layer2 {

//==============================================================================
// Telemetry Bridge Interface
//==============================================================================

class ITelemetryBridge {
public:
    //==========================================================================
    // Type Definitions
    //==========================================================================

    // Extended telemetry frame with bridge-specific fields
    struct BridgeTelemetryFrame {
        uint64_t sequenceNumber;    // Global ordering (for drop detection)
        NodeID sourceId;            // Source identifier (track ID, node ID, etc.)
        uint8_t type;               // TelemetryFrame::Type
        uint8_t priority;           // 0=critical, 255=background
        uint16_t flags;             // Additional flags
        uint32_t payload[4];        // Type-specific data (16 bytes)
    };

    //==========================================================================
    // Producer Operations (Audio/Worker Threads)
    //==========================================================================

    // Push a single telemetry frame to the queue
    // Returns: true if queued successfully, false if queue is full
    // Thread-safety: Multiple producers (Audio threads)
    // RT-safety: YES (wait-free, no allocations)
    virtual bool pushTelemetry(const BridgeTelemetryFrame& frame) = 0;

    // Push telemetry with explicit source and type
    // Returns: true if queued successfully, false if queue is full
    // Thread-safety: Multiple producers (Audio threads)
    // RT-safety: YES (wait-free, no allocations)
    virtual bool pushTelemetry(NodeID sourceId,
                               uint8_t type,
                               const uint32_t* payload,
                               uint8_t priority = 128) = 0;

    // Push telemetry with automatic sequence numbering
    // Returns: true if queued successfully, false if queue is full
    // Thread-safety: Multiple producers (Audio threads)
    // RT-safety: YES (wait-free, no allocations)
    virtual bool pushTelemetryAutoSeq(NodeID sourceId,
                                     uint8_t type,
                                     const uint32_t* payload,
                                     uint8_t priority = 128) = 0;

    //==========================================================================
    // Consumer Operations (GUI/Main Thread)
    //==========================================================================

    // Poll telemetry frames from the queue
    // Returns: Number of frames actually retrieved (0 to maxFrames)
    // Note: If minSequenceNumber is specified, only frames with sequence >= this are returned
    // Thread-safety: Single consumer (GUI thread)
    // RT-safety: NO (may be called from non-RT thread)
    virtual uint32_t pollTelemetry(BridgeTelemetryFrame* outFrames,
                                   uint32_t maxFrames,
                                   uint64_t minSequenceNumber = 0) = 0;

    // Poll telemetry with source filtering
    // Only returns telemetry from subscribed sources
    // Returns: Number of frames actually retrieved
    // Thread-safety: Single consumer (GUI thread)
    // RT-safety: NO (may be called from non-RT thread)
    virtual uint32_t pollTelemetryFiltered(BridgeTelemetryFrame* outFrames,
                                           uint32_t maxFrames) = 0;

    // Get latest Peak and RMS values directly from the lock-free state array (O(1))
    // Returns true if the source exists and has data, false otherwise
    // Thread-safety: Safe to call from any thread (lock-free, wait-free)
    virtual bool getLatestMeterValues(NodeID sourceId,
                                      float& outPeakLeft, float& outPeakRight,
                                      float& outRmsLeft, float& outRmsRight,
                                      bool& outClipLeft, bool& outClipRight,
                                      uint64_t* outSequence = nullptr) const = 0;

    //==========================================================================
    // Source Subscription Management
    //==========================================================================

    // Set which sources to receive telemetry from
    // Passing null or count=0 subscribes to all sources
    // Thread-safety: Safe to call from any thread
    virtual void setSubscribedSources(const NodeID* sourceIds,
                                     uint32_t count) = 0;

    // Add a source to the subscription list
    // Thread-safety: Safe to call from any thread
    virtual void addSubscribedSource(NodeID sourceId) = 0;

    // Remove a source from the subscription list
    // Thread-safety: Safe to call from any thread
    virtual void removeSubscribedSource(NodeID sourceId) = 0;

    // Clear all subscriptions (subscribe to all sources)
    // Thread-safety: Safe to call from any thread
    virtual void clearSubscriptions() = 0;

    // Check if a source is currently subscribed
    // Returns: true if subscribed, false otherwise
    // Thread-safety: Safe to call from any thread
    virtual bool isSubscribed(NodeID sourceId) const = 0;

    //==========================================================================
    // Query Operations
    //==========================================================================

    // Get current queue depth (number of pending frames)
    // Returns: Current number of frames in queue
    // Note: This is a snapshot, may change immediately after call
    // Thread-safety: Safe to call from any thread
    virtual uint32_t getDepth() const = 0;

    // Get queue capacity
    // Returns: Maximum number of frames queue can hold
    // Thread-safety: Safe to call from any thread
    virtual uint32_t getCapacity() const = 0;

    // Get the next sequence number that will be assigned
    // Returns: Next sequence number
    // Thread-safety: Safe to call from any thread
    virtual uint64_t getNextSequenceNumber() const = 0;

    // Get the number of currently subscribed sources
    // Returns: 0 if subscribed to all sources, otherwise count of specific sources
    // Thread-safety: Safe to call from any thread
    virtual uint32_t getSubscriptionCount() const = 0;

    //==========================================================================
    // Statistics
    //==========================================================================

    struct Statistics {
        uint64_t totalPushed;          // Total frames pushed
        uint64_t totalPolled;          // Total frames polled
        uint64_t dropped;              // Frames dropped (queue full)
        uint64_t filteredOut;          // Frames filtered by subscription
        uint64_t sequenceGaps;         // Sequence number gaps detected
        uint32_t peakDepth;            // Maximum queue depth observed
        uint32_t currentDepth;         // Current queue depth
        uint32_t activeSourceCount;    // Number of active sources seen
    };

    // Get bridge statistics
    // Returns: Current statistics snapshot
    // Thread-safety: Safe to call from any thread
    virtual void getStatistics(Statistics& outStats) const = 0;

    // Reset statistics counters
    // Thread-safety: Safe to call from any thread
    virtual void resetStatistics() = 0;

    //==========================================================================
    // Configuration
    //==========================================================================

    // Enable/disable frame dropping when queue is full
    // When enabled: New frames replace oldest frames (circular behavior)
    // When disabled: Push fails when queue is full
    // Thread-safety: Safe to call from any thread
    virtual void setDroppingEnabled(bool enabled) = 0;

    // Check if dropping is enabled
    // Returns: true if enabled, false if disabled
    // Thread-safety: Safe to call from any thread
    virtual bool isDroppingEnabled() const = 0;

    //==========================================================================
    // Factory
    //==========================================================================

    // Create a telemetry bridge with specified capacity
    // Capacity must be a power of 2 for MPSC queue optimization
    // Returns: Unique pointer to the bridge instance
    // Recommended capacities: 256, 512, 1024, 2048, 4096
    static std::unique_ptr<ITelemetryBridge> create(uint32_t capacity = 1024);

    //==========================================================================
    // Destructor
    //==========================================================================

    virtual ~ITelemetryBridge() = default;
};

//==============================================================================
// Telemetry Priorities
//==============================================================================

namespace TelemetryPriority {
    constexpr uint8_t CRITICAL       = 0;    // Transport state, errors
    constexpr uint8_t HIGH           = 32;   // Playhead, timecode
    constexpr uint8_t NORMAL         = 128;  // Peak meters, waveform updates
    constexpr uint8_t LOW            = 192;  // CPU load, memory usage
    constexpr uint8_t BACKGROUND     = 255;  // Debug info, statistics
}

//==============================================================================
// Telemetry Frame Flags
//==============================================================================

namespace TelemetryFlags {
    constexpr uint16_t NONE                   = 0x0000;
    constexpr uint16_t IS_LATEST              = 0x0001;  // Supersedes previous from this source
    constexpr uint16_t IS_INTERPOLATED        = 0x0002;  // Value is interpolated
    constexpr uint16_t IS_CLIPPED             = 0x0004;  // Value was clipped
    constexpr uint16_t IS_STALE               = 0x0008;  // Data may be outdated
    constexpr uint16_t REQUIRES_ACK           = 0x0010;  // Requires acknowledgment
    constexpr uint16_t PARTIAL_BATCH          = 0x0020;  // Part of a larger batch
}

//==============================================================================
// Helper Functions for Common Telemetry Types
//==============================================================================

namespace TelemetryHelpers {

    // Create peak meter frame
    inline ITelemetryBridge::BridgeTelemetryFrame makePeakMeter(NodeID sourceId,
                                              float peakLevelDb,
                                              bool clipped = false)
    {
        ITelemetryBridge::BridgeTelemetryFrame frame = {};
        frame.sourceId = sourceId;
        frame.type = TelemetryFrame::PEAK_METER;
        frame.priority = TelemetryPriority::NORMAL;

        // Pack float into uint32_t (bitcast)
        uint32_t levelBits;
        std::memcpy(&levelBits, &peakLevelDb, sizeof(float));
        frame.payload[0] = levelBits;
        frame.payload[1] = clipped ? 1 : 0;

        return frame;
    }

    // Create playhead position frame
    inline ITelemetryBridge::BridgeTelemetryFrame makePlayhead(uint64_t positionSample,
                                            double bpm,
                                            uint32_t bar, uint32_t beat, uint32_t tick)
    {
        ITelemetryBridge::BridgeTelemetryFrame frame = {};
        frame.sourceId = NodeID::invalid();  // Global
        frame.type = TelemetryFrame::PLAYHEAD_POSITION;
        frame.priority = TelemetryPriority::HIGH;

        frame.payload[0] = static_cast<uint32_t>(positionSample & 0xFFFFFFFF);
        frame.payload[1] = static_cast<uint32_t>(positionSample >> 32);

        // Pack float into uint32_t
        float bpmFloat = static_cast<float>(bpm);
        uint32_t bpmBits;
        std::memcpy(&bpmBits, &bpmFloat, sizeof(float));
        frame.payload[2] = bpmBits;

        frame.payload[3] = (bar << 20) | (beat << 8) | tick;

        return frame;
    }

    // Create CPU load frame
    inline ITelemetryBridge::BridgeTelemetryFrame makeCPULoad(float cpuPercent)
    {
        ITelemetryBridge::BridgeTelemetryFrame frame = {};
        frame.sourceId = NodeID::invalid();  // Global
        frame.type = TelemetryFrame::CPU_LOAD;
        frame.priority = TelemetryPriority::LOW;

        uint32_t cpuBits;
        std::memcpy(&cpuBits, &cpuPercent, sizeof(float));
        frame.payload[0] = cpuBits;

        return frame;
    }

    // Create playback state frame
    inline ITelemetryBridge::BridgeTelemetryFrame makePlaybackState(uint8_t transportState,
                                                  bool isRecording)
    {
        ITelemetryBridge::BridgeTelemetryFrame frame = {};
        frame.sourceId = NodeID::invalid();  // Global
        frame.type = TelemetryFrame::PLAYBACK_STATE;
        frame.priority = TelemetryPriority::CRITICAL;

        frame.payload[0] = transportState;
        frame.payload[1] = isRecording ? 1 : 0;

        return frame;
    }

} // namespace TelemetryHelpers

} // namespace Layer2
