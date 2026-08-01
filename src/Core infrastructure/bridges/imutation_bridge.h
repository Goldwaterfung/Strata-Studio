// imutation_bridge.h v1.0
// Mutation Bridge Interface - GUI to Audio Thread Communication
//
// PURPOSE:
// - Provides lock-free command queue from GUI (producer) to Audio (consumer)
// - Ensures ordered mutation delivery with sequence numbering
// - Supports priority-based dropping when queue is full
// - Enables dependency tracking between mutations
//
// THREAD SAFETY:
// - push(): Called from GUI/Main thread (single producer)
// - pop(): Called from Audio thread (single consumer)
// - Thread-safe when used by one producer and one consumer
//
// REAL-TIME GUARANTEES:
// - pop() is wait-free and RT-safe
// - push() is wait-free (may drop low-priority mutations when full)

#pragma once

#include "system_primitives.h"
#include <memory>
#include <cstdint>

namespace Layer2 {

//==============================================================================
// Mutation Bridge Interface
//==============================================================================

class IMutationBridge {
public:
    //==========================================================================
    // Type Definitions
    //==========================================================================

    using MutationId = uint64_t;
    constexpr static MutationId INVALID_MUTATION_ID = UINT64_MAX;

    //==========================================================================
    // Push Result - Return status for push operations
    //==========================================================================

    enum class PushResult : uint8_t {
        SUCCESS,                    // Mutation successfully queued
        DROPPED_LOW_PRIORITY,       // Mutation dropped (low priority, queue full)
        FAILED_CRITICAL_DROPPED     // Critical mutation dropped (queue full)
    };

    //==========================================================================
    // Mutation Batch Builder - For efficient batch submission
    //==========================================================================

    class MutationBatchBuilder {
    public:
        virtual ~MutationBatchBuilder() = default;

        // Add a mutation to the batch
        // Returns: true if added, false if batch is full
        virtual bool addMutation(const SystemMutation& mutation) = 0;

        // Get current batch size
        virtual uint32_t size() const = 0;

        // Clear all mutations from batch
        virtual void clear() = 0;

        // Get pointer to mutations array
        virtual const SystemMutation* getMutations() const = 0;

        // Get number of mutations in batch
        virtual uint32_t getMutationCount() const = 0;
    };

    //==========================================================================
    // Producer Operations (GUI/Main Thread)
    //==========================================================================

    // Push a single mutation to the queue
    // Returns: true if queued successfully, false if queue is full
    // Thread-safety: Single producer (GUI thread)
    // RT-safety: No (may be called from non-RT thread)
    virtual bool pushMutation(const SystemMutation& mutation) = 0;

    // Push a mutation with explicit priority
    // If queue is full:
    //   - Low priority mutations may be dropped
    //   - Critical mutations (priority < 32) are never dropped
    // Returns: PushResult indicating success or reason for failure
    // Thread-safety: Single producer (GUI thread)
    // RT-safety: No (may be called from non-RT thread)
    virtual PushResult pushMutationWithPriority(const SystemMutation& mutation,
                                                uint8_t priority) = 0;

    // Push a batch of mutations atomically
    // Returns: true if all mutations queued, false if batch didn't fit
    // Thread-safety: Single producer (GUI thread)
    // RT-safety: No (may be called from non-RT thread)
    virtual bool pushBatch(const MutationBatchBuilder& batch) = 0;

    // Push a mutation that depends on another mutation completing first
    // The mutation will not be processed until dependencyId completes
    // Returns: true if queued successfully, false if queue is full
    // Thread-safety: Single producer (GUI thread)
    // RT-safety: No (may be called from non-RT thread)
    virtual bool pushDependentMutation(const SystemMutation& mutation,
                                      MutationId dependsOn) = 0;

    //==========================================================================
    // Consumer Operations (Audio Thread)
    //==========================================================================

    /**
     * @brief Latch the current mutations for the upcoming cycle (Phase 1)
     * @thread_safety Single consumer (Audio thread)
     * @RT_safety YES (wait-free)
     */
    virtual void prepareCycle() = 0;

    /**
     * @brief Pop a mutation that was latched during prepareCycle()
     * @return true if mutation retrieved, false if no more committed mutations
     */
    virtual bool popMutationForCycle(SystemMutation& outMutation) = 0;

    // Pop a single mutation from the queue (immediate, non-latched)
    virtual bool popMutation(SystemMutation& outMutation) = 0;

    // Pop multiple mutations in a single call (batch processing)
    // Returns: Number of mutations actually popped (0 to maxMutations)
    // Thread-safety: Single consumer (Audio thread)
    // RT-safety: YES (wait-free, no allocations)
    virtual uint32_t popMultiple(SystemMutation* outMutations,
                                 uint32_t maxMutations) = 0;

    //==========================================================================
    // Batch Builder Factory
    //==========================================================================

    // Create a new batch builder for efficient batch submission
    // Returns: Unique pointer to a batch builder instance
    // Thread-safety: Safe to call from any thread
    virtual std::unique_ptr<MutationBatchBuilder> createBatchBuilder() = 0;

    //==========================================================================
    // Query Operations
    //==========================================================================

    // Get current queue depth (number of pending mutations)
    // Returns: Current number of mutations in queue
    // Note: This is a snapshot, may change immediately after call
    // Thread-safety: Safe to call from any thread
    virtual uint32_t getDepth() const = 0;

    // Get queue capacity
    // Returns: Maximum number of mutations queue can hold
    // Thread-safety: Safe to call from any thread
    virtual uint32_t getCapacity() const = 0;

    // Get the next sequence number that will be assigned
    // Returns: Next sequence number
    // Thread-safety: Safe to call from any thread
    virtual uint64_t getNextSequenceNumber() const = 0;

    //==========================================================================
    // Statistics
    //==========================================================================

    struct Statistics {
        uint64_t totalPushed;          // Total mutations pushed
        uint64_t totalPopped;          // Total mutations popped
        uint64_t droppedLowPriority;   // Low-priority mutations dropped
        uint64_t droppedCritical;      // Critical mutations dropped (should be 0)
        uint64_t batchPushes;          // Number of batch push operations
        uint32_t peakDepth;            // Maximum queue depth observed
        uint32_t currentDepth;         // Current queue depth
    };

    // Get bridge statistics
    // Returns: Current statistics snapshot
    // Thread-safety: Safe to call from any thread
    virtual void getStatistics(Statistics& outStats) const = 0;

    // Reset statistics counters
    // Thread-safety: Safe to call from any thread
    virtual void resetStatistics() = 0;

    //==========================================================================
    // Flow Control
    //==========================================================================

    // Wait for space to become available in the queue
    // Returns: true if space is available, false if timeout
    // Thread-safety: Single producer (GUI thread)
    // RT-safety: NO (blocks, do not call from audio thread)
    virtual bool waitForSpace(uint32_t timeoutMs) = 0;

    // Enable/disable priority-based dropping
    // When enabled: Low priority mutations are dropped when queue is full
    // When disabled: All mutations are rejected when queue is full
    // Thread-safety: Safe to call from any thread
    virtual void setPriorityDropping(bool enabled) = 0;

    // Check if priority-based dropping is enabled
    // Returns: true if enabled, false if disabled
    // Thread-safety: Safe to call from any thread
    virtual bool isPriorityDroppingEnabled() const = 0;

    //==========================================================================
    // Factory
    //==========================================================================

    // Create a mutation bridge with specified capacity
    // Capacity must be a power of 2 for SPSC queue optimization
    // Returns: Unique pointer to the bridge instance
    // Recommended capacities: 256, 512, 1024, 2048, 4096
    static std::unique_ptr<IMutationBridge> create(uint32_t capacity = 1024);

    //==========================================================================
    // Destructor
    //==========================================================================

    virtual ~IMutationBridge() = default;
};

//==============================================================================
// Default Mutation Priorities
//==============================================================================

namespace MutationPriority {
    constexpr uint8_t CRITICAL       = 0;    // Transport, emergency stop
    constexpr uint8_t HIGH           = 32;   // Graph changes, plugin load/unload
    constexpr uint8_t NORMAL         = 128;  // Parameter changes, automation
    constexpr uint8_t LOW            = 192;  // UI updates, non-critical
    constexpr uint8_t BACKGROUND     = 255;  // Background tasks, cleanup
}

//==============================================================================
// Mutation Types (for SystemMutation::type field)
//==============================================================================

namespace MutationType {
    // Transport mutations
    constexpr uint8_t TRANSPORT_START         = 0;
    constexpr uint8_t TRANSPORT_STOP          = 1;
    constexpr uint8_t TRANSPORT_RECORD        = 2;
    constexpr uint8_t TRANSPORT_SEEK          = 3;

    // Graph mutations
    constexpr uint8_t NODE_ADD                = 10;
    constexpr uint8_t NODE_REMOVE             = 11;
    constexpr uint8_t NODE_CONNECT            = 12;
    constexpr uint8_t NODE_DISCONNECT         = 13;
    constexpr uint8_t NODE_REORDER            = 14;
    constexpr uint8_t SIDECHAIN_CONNECT       = 15;
    constexpr uint8_t SIDECHAIN_DISCONNECT    = 16;

    // Plugin mutations
    constexpr uint8_t PLUGIN_LOAD             = 20;
    constexpr uint8_t PLUGIN_UNLOAD           = 21;
    constexpr uint8_t PLUGIN_BYPASS           = 22;
    constexpr uint8_t PLUGIN_REPLACE          = 23;

    // Parameter mutations
    constexpr uint8_t PARAMETER_SET           = 30;
    constexpr uint8_t PARAMETER_RAMP_BEGIN    = 31;
    constexpr uint8_t PARAMETER_RAMP_END      = 32;

    // Track/Bus mutations
    constexpr uint8_t TRACK_ADD               = 40;
    constexpr uint8_t TRACK_REMOVE            = 41;
    constexpr uint8_t TRACK_RENAME            = 42;
    constexpr uint8_t BUS_ADD                 = 43;
    constexpr uint8_t BUS_REMOVE              = 44;

    // Tempo/Time signature mutations
    constexpr uint8_t TEMPO_SET               = 50;
    constexpr uint8_t TEMPO_MAP_ADD           = 51;
    constexpr uint8_t TIME_SIGNATURE_SET       = 52;

    // State mutations
    constexpr uint8_t SNAPSHOT_CREATE         = 60;
    constexpr uint8_t SNAPSHOT_RESTORE        = 61;
    constexpr uint8_t UNDO                    = 62;
    constexpr uint8_t REDO                    = 63;

    // Monitoring mutations
    constexpr uint8_t MONITOR_STATE_SET       = 70; // payload: monitor.monitorState
    constexpr uint8_t LIVE_MIDI_TARGETS       = 71; // payload: packed NodeID list (future)
    constexpr uint8_t RECORD_ARM_SET          = 72; // Set record arm status on AudioInputNode


    // Misc
    constexpr uint8_t CUSTOM                  = 255;
}

//==============================================================================
// Mutation Flags (for SystemMutation::flags field)
//==============================================================================

namespace MutationFlags {
    constexpr uint16_t NONE                   = 0x0000;
    constexpr uint16_t DEFERRED               = 0x0001;  // Process at buffer boundary
    constexpr uint16_t ATOMIC                 = 0x0002;  // Must complete atomically
    constexpr uint16_t REVERSIBLE             = 0x0004;  // Can be undone
    constexpr uint16_t REQUIRES_SYNC          = 0x0008;  // Requires sync point
    constexpr uint16_t IS_DEPENDENT           = 0x0010;  // Has dependency
    constexpr uint16_t BATCH_PART             = 0x0020;  // Part of a batch
}

} // namespace Layer2
