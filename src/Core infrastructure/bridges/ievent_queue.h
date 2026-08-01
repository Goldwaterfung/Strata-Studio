// ievent_queue.h v1.0
// Event Queue Interface - High-Frequency Event Delivery to Audio Thread
//
// PURPOSE:
// - Provides lock-free event queue for sample-accurate parameter and MIDI delivery
// - Supports multiple producers (automation, GUI, MIDI) to single consumer (audio)
// - Optimized for high-frequency events (100+ events per buffer)
// - Enables zero-copy event routing by targetNodeId during DSP graph traversal
//
// THREAD SAFETY:
// - pushEvent(): Called from multiple producer threads (automation, GUI, MIDI)
// - popMultiple(): Called from Audio thread (single consumer)
// - Thread-safe when used by multiple producers and one consumer
//
// REAL-TIME GUARANTEES:
// - popMultiple() is wait-free and RT-safe (no allocations, no blocking)
// - pushEvent() is wait-free (may drop events when full)
//
// DESIGN PRINCIPLES:
// - Events are sample-accurate (carry sampleOffset for timing)
// - Events are routed by targetNodeId (Layer 3 kernel filters during traversal)
// - Overflow handling: Drop oldest events (automation is latest-value semantic)
// - Separate from IMutationBridge (topology changes vs parameter events)

#pragma once

#include "system_primitives.h"
#include <cstdint>
#include <memory>

namespace Layer2 {

//==============================================================================
// Event Queue Interface
//==============================================================================

class IEventQueue {
public:
    //==========================================================================
    // Type Definitions
    //==========================================================================

    using EventId = uint64_t;
    constexpr static EventId INVALID_EVENT_ID = UINT64_MAX;

    //==========================================================================
    // Queue Configuration
    //==========================================================================

    struct Config {
        uint32_t capacity;              // Queue capacity (must be power of 2)
        bool dropOldestWhenFull;        // Drop oldest events when full (circular)
        uint32_t maxEventsPerPop;       // RT safety limit for batch pop
        bool enableSequenceNumbers;     // Enable event sequencing for telemetry

        // Default configuration
        static constexpr Config defaultConfig() {
            return { 4096, true, 512, true };
        }
    };

    //==========================================================================
    // Statistics
    //==========================================================================

    struct Statistics {
        uint64_t totalPushed;           // Total events pushed
        uint64_t totalPopped;           // Total events popped
        uint64_t dropped;               // Events dropped (queue full)
        uint64_t sequenceGaps;          // Sequence number gaps detected
        uint32_t peakDepth;             // Maximum queue depth observed
        uint32_t currentDepth;          // Current queue depth
        uint32_t overflowCount;         // Number of times queue overflowed
    };

    //==========================================================================
    // Producer Operations (Multiple Producers)
    //==========================================================================

    // Push a single event to the queue
    // Returns: true if queued successfully, false if queue is full
    // Thread-safety: Multiple producers (automation, GUI, MIDI threads)
    // RT-safety: YES (wait-free, no allocations)
    virtual bool pushEvent(const EventData& event) = 0;

    // Push event with explicit priority (for future priority-based filtering)
    // Currently all events are treated equally, but this enables future features
    // Returns: true if queued successfully, false if queue is full
    // Thread-safety: Multiple producers
    // RT-safety: YES (wait-free, no allocations)
    virtual bool pushEventWithPriority(const EventData& event, uint8_t priority) = 0;

    // Push multiple events atomically
    // Returns: Number of events actually queued (may be less than count if full)
    // Thread-safety: Multiple producers
    // RT-safety: YES (wait-free, no allocations)
    virtual uint32_t pushBatch(const EventData* events, uint32_t count) = 0;

    //==========================================================================
    // Consumer Operations (Audio Thread)
    //==========================================================================

    /**
     * @brief Latch all events currently in the queue for the current cycle (Phase 1)
     * @thread_safety Single consumer (Audio thread)
     * @RT_safety YES (wait-free)
     */
    virtual void prepareCycle() = 0;

    /**
     * @brief Pop an event that was latched during prepareCycle()
     * @return true if event retrieved, false if no more committed events
     */
    virtual bool popEventForCycle(EventData& outEvent) = 0;

    // Pop multiple events from the queue (immediate, non-latched)
    virtual uint32_t popMultiple(EventData* outEvents, uint32_t maxEvents, uint32_t numSamples = 1024) = 0;

    // Pop events with sequence number filtering
    // Returns: Number of events retrieved
    // Only returns events with sequence >= minSequenceNumber
    // Thread-safety: Single consumer (Audio thread)
    // RT-safety: YES (wait-free, no allocations)
    virtual uint32_t popMultipleWithSequence(EventData* outEvents,
                                              uint32_t maxEvents,
                                              uint64_t minSequenceNumber) = 0;

    //==========================================================================
    // Query Operations
    //==========================================================================

    // Get current queue depth (number of pending events)
    // Returns: Current number of events in queue
    // Note: This is a snapshot, may change immediately after call
    // Thread-safety: Safe to call from any thread
    virtual uint32_t getDepth() const = 0;

    // Get queue capacity
    // Returns: Maximum number of events queue can hold
    // Thread-safety: Safe to call from any thread
    virtual uint32_t getCapacity() const = 0;

    // Get the next sequence number that will be assigned
    // Returns: Next sequence number (if enabled)
    // Thread-safety: Safe to call from any thread
    virtual uint64_t getNextSequenceNumber() const = 0;

    //==========================================================================
    // Statistics
    //==========================================================================

    // Get queue statistics
    // Returns: Current statistics snapshot
    // Thread-safety: Safe to call from any thread
    virtual void getStatistics(Statistics& outStats) const = 0;

    // Reset statistics counters
    // Thread-safety: Safe to call from any thread
    virtual void resetStatistics() = 0;

    //==========================================================================
    // Configuration
    //==========================================================================

    // Enable/disable event dropping when queue is full
    // When enabled: New events replace oldest events (circular behavior)
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

    // Create an event queue with specified configuration
    // Capacity must be a power of 2 for MPSC queue optimization
    // Returns: Unique pointer to the queue instance
    // Recommended capacities: 1024, 2048, 4096, 8192
    static std::unique_ptr<IEventQueue> create(const Config& config = Config::defaultConfig());

    //==========================================================================
    // Destructor
    //==========================================================================

    virtual ~IEventQueue() = default;
};

//==============================================================================
// Event Priorities (for future use)
//==============================================================================

namespace EventPriority {
    constexpr uint8_t CRITICAL       = 0;    // Transport events, emergency stop
    constexpr uint8_t HIGH           = 32;   // MIDI Note On/Off, Pitch Bend
    constexpr uint8_t NORMAL         = 128;  // Automation, CC messages
    constexpr uint8_t LOW            = 192;  // Metadata, non-critical
    constexpr uint8_t BACKGROUND     = 255;  // Debug info, statistics
}

//==============================================================================
// Helper Functions for Common Event Types
//==============================================================================

namespace EventHelpers {

    // Create automation event
    inline EventData makeAutomationEvent(NodeID targetNodeId,
                                         uint32_t parameterIndex,
                                         float targetValue,
                                         uint32_t sampleOffset = 0,
                                         uint32_t rampDuration = 0)
    {
        EventData event = {};
        event.targetNodeId = targetNodeId;
        event.sampleOffset = sampleOffset;
        event.eventType = EventType::AUTOMATION;
        event.flags = 0;

        event.payload.automation.parameterIndex = parameterIndex;
        event.payload.automation.targetValue = targetValue;
        event.payload.automation.rampDuration = rampDuration;
        event.payload.automation.targetSubNodeId = 0;

        return event;
    }

    // Create MIDI Note On event
    inline EventData makeMIDINoteOn(NodeID targetNodeId,
                                    uint8_t pitch,
                                    uint8_t velocity,
                                    uint8_t channel = 0,
                                    uint32_t sampleOffset = 0,
                                    uint8_t flags = 0x00)
    {
        EventData event = {};
        event.targetNodeId = targetNodeId;
        event.sampleOffset = sampleOffset;
        event.eventType = EventType::MIDI_NOTE_ON;
        event.flags = flags;  // Note on flag

        event.payload.midiNote.pitch = pitch;
        event.payload.midiNote.velocity = velocity;
        event.payload.midiNote.channel = channel;
        event.payload.midiNote.reserved = 0;

        return event;
    }

    // Create MIDI Note Off event
    inline EventData makeMIDINoteOff(NodeID targetNodeId,
                                     uint8_t pitch,
                                     uint8_t velocity,
                                     uint8_t channel = 0,
                                     uint32_t sampleOffset = 0)
    {
        EventData event = {};
        event.targetNodeId = targetNodeId;
        event.sampleOffset = sampleOffset;
        event.eventType = EventType::MIDI_NOTE_OFF;
        event.flags = 0x00;  // Note off flag

        event.payload.midiNote.pitch = pitch;
        event.payload.midiNote.velocity = velocity;
        event.payload.midiNote.channel = channel;
        event.payload.midiNote.reserved = 0;

        return event;
    }

    // Create MIDI CC event
    inline EventData makeMIDICC(NodeID targetNodeId,
                                uint8_t controllerNumber,
                                uint8_t value,
                                uint8_t channel = 0,
                                uint32_t sampleOffset = 0)
    {
        EventData event = {};
        event.targetNodeId = targetNodeId;
        event.sampleOffset = sampleOffset;
        event.eventType = EventType::MIDI_CC;
        event.flags = 0;

        event.payload.midiCC.controllerNumber = controllerNumber;
        event.payload.midiCC.value = value;
        event.payload.midiCC.channel = channel;
        event.payload.midiCC.reserved = 0;

        return event;
    }

    // Create MIDI Pitch Bend event
    inline EventData makeMIDIPitchBend(NodeID targetNodeId,
                                      uint16_t value,
                                      uint8_t channel = 0,
                                      uint32_t sampleOffset = 0)
    {
        EventData event = {};
        event.targetNodeId = targetNodeId;
        event.sampleOffset = sampleOffset;
        event.eventType = EventType::MIDI_PITCH;
        event.flags = 0;

        event.payload.midiPitch.value = value;
        event.payload.midiPitch.channel = channel;
        event.payload.midiPitch.reserved = 0;
        event.payload.midiPitch.reserved2 = 0;

        return event;
    }

} // namespace EventHelpers

} // namespace Layer2
