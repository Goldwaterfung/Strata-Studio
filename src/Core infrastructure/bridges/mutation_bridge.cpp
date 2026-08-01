// mutation_bridge.cpp v1.0
// Mutation Bridge Implementation - GUI to Audio Thread Communication
//
// IMPLEMENTATION NOTES:
// - Uses SPSC lock-free queue for wait-free operations
// - Sequence numbering for ordered delivery
// - Priority-based dropping when queue is full
// - Statistics tracking for monitoring

#include "Core infrastructure/bridges/imutation_bridge.h"
#include "Core infrastructure/bridges/spsc_queue.h"
#include <atomic>
#include <cstring>
#include <vector>
#include <mutex>
#include <condition_variable>

namespace Layer2 {

//==============================================================================
// Internal Constants
//==============================================================================

namespace {
    // Priority threshold for critical mutations (never dropped)
    constexpr uint8_t CRITICAL_PRIORITY_THRESHOLD = 32;

    // Maximum batch size (prevents excessive memory allocation)
    constexpr uint32_t MAX_BATCH_SIZE = 256;
}

//==============================================================================
// Mutation Batch Builder Implementation
//==============================================================================

class MutationBatchBuilderImpl : public IMutationBridge::MutationBatchBuilder {
private:
    std::vector<SystemMutation> mutations_;

public:
    MutationBatchBuilderImpl()
    {
        mutations_.reserve(MAX_BATCH_SIZE);
    }

    ~MutationBatchBuilderImpl() override = default;

    bool addMutation(const SystemMutation& mutation) override
    {
        if (mutations_.size() >= MAX_BATCH_SIZE) {
            return false;  // Batch full
        }
        mutations_.push_back(mutation);
        return true;
    }

    uint32_t size() const override
    {
        return static_cast<uint32_t>(mutations_.size());
    }

    void clear() override
    {
        mutations_.clear();
    }

    const SystemMutation* getMutations() const override
    {
        return mutations_.data();
    }

    uint32_t getMutationCount() const override
    {
        return static_cast<uint32_t>(mutations_.size());
    }
};

//==============================================================================
// Mutation Bridge Implementation
//==============================================================================

template<uint32_t Capacity>
class MutationBridgeImpl : public IMutationBridge {
private:
    // The underlying SPSC queue
    SPSCQueue<SystemMutation, Capacity> queue_;

    // Sequence number counter (atomic for thread-safe access)
    std::atomic<uint64_t> sequenceCounter_;

    // Statistics
    std::atomic<uint64_t> totalPushed_;
    std::atomic<uint64_t> totalPopped_;
    std::atomic<uint64_t> droppedLowPriority_;
    std::atomic<uint64_t> droppedCritical_;
    std::atomic<uint64_t> batchPushes_;
    std::atomic<uint32_t> peakDepth_;

    // Configuration
    std::atomic<bool> priorityDroppingEnabled_;

    // Phase 1 Synchronization
    std::atomic<uint32_t> latchedWriteIndex_;

    // Flow control synchronization
    std::mutex spaceMutex_;
    std::condition_variable spaceCV_;

    //==========================================================================
    // Helper: Update peak depth statistic
    //==========================================================================

    void updatePeakDepth()
    {
        const uint32_t currentDepth = queue_.depth();
        uint32_t currentPeak = peakDepth_.load(std::memory_order_relaxed);
        while (currentDepth > currentPeak) {
            if (peakDepth_.compare_exchange_weak(currentPeak, currentDepth,
                                                  std::memory_order_relaxed)) {
                break;
            }
        }
    }

public:
    //==========================================================================
    // Constructor
    //==========================================================================

    MutationBridgeImpl()
        : sequenceCounter_(0)
        , totalPushed_(0)
        , totalPopped_(0)
        , droppedLowPriority_(0)
        , droppedCritical_(0)
        , batchPushes_(0)
        , peakDepth_(0)
        , priorityDroppingEnabled_(true)
        , latchedWriteIndex_(0)
    {}

    //==========================================================================
    // Phase 1: Latch mutations for current cycle
    //==========================================================================

    void prepareCycle() override
    {
        latchedWriteIndex_.store(queue_.getWriteIndex(), std::memory_order_release);
    }

    bool popMutationForCycle(SystemMutation& outMutation) override
    {
        const bool popped = queue_.popLimited(outMutation, latchedWriteIndex_.load(std::memory_order_acquire));

        if (popped) {
            totalPopped_.fetch_add(1, std::memory_order_relaxed);
            spaceCV_.notify_one();
        }

        return popped;
    }

    //==========================================================================
    // Producer: Push a single mutation
    //==========================================================================

    bool pushMutation(const SystemMutation& mutation) override
    {
        SystemMutation m = mutation;
        m.sequenceNumber = sequenceCounter_.fetch_add(1, std::memory_order_relaxed);

        const bool pushed = queue_.push(m);

        if (pushed) {
            totalPushed_.fetch_add(1, std::memory_order_relaxed);
            updatePeakDepth();
            spaceCV_.notify_one();
        }

        return pushed;
    }

    //==========================================================================
    // Producer: Push with priority-based dropping
    //==========================================================================

    PushResult pushMutationWithPriority(const SystemMutation& mutation,
                                       uint8_t priority) override
    {
        SystemMutation m = mutation;
        m.priority = priority;
        m.sequenceNumber = sequenceCounter_.fetch_add(1, std::memory_order_relaxed);

        bool pushed = queue_.push(m);

        if (!pushed) {
            // Queue is full, decide based on priority
            if (priority < CRITICAL_PRIORITY_THRESHOLD) {
                // Critical mutation - should not be dropped
                droppedCritical_.fetch_add(1, std::memory_order_relaxed);
                return PushResult::FAILED_CRITICAL_DROPPED;
            } else if (priorityDroppingEnabled_.load(std::memory_order_relaxed)) {
                // Low priority mutation - safe to drop
                droppedLowPriority_.fetch_add(1, std::memory_order_relaxed);
                return PushResult::DROPPED_LOW_PRIORITY;
            } else {
                // Priority dropping disabled, report as critical failure
                droppedCritical_.fetch_add(1, std::memory_order_relaxed);
                return PushResult::FAILED_CRITICAL_DROPPED;
            }
        }

        totalPushed_.fetch_add(1, std::memory_order_relaxed);
        updatePeakDepth();
        spaceCV_.notify_one();

        return PushResult::SUCCESS;
    }

    //==========================================================================
    // Producer: Push a batch of mutations
    //==========================================================================

    bool pushBatch(const MutationBatchBuilder& batch) override
    {
        const auto* mutations = batch.getMutations();
        const uint32_t count = batch.getMutationCount();

        if (count == 0) {
            return true;  // Empty batch always succeeds
        }

        // Check if batch would fit
        if (queue_.availableWrite() < count) {
            return false;  // Batch won't fit
        }

        // Push all mutations in sequence
        for (uint32_t i = 0; i < count; ++i) {
            SystemMutation m = mutations[i];
            m.sequenceNumber = sequenceCounter_.fetch_add(1, std::memory_order_relaxed);

            if (!queue_.push(m)) {
                // Should not happen if we checked availableWrite()
                return false;
            }

            totalPushed_.fetch_add(1, std::memory_order_relaxed);
        }

        batchPushes_.fetch_add(1, std::memory_order_relaxed);
        updatePeakDepth();
        spaceCV_.notify_one();

        return true;
    }

    //==========================================================================
    // Producer: Push a dependent mutation
    //==========================================================================

    bool pushDependentMutation(const SystemMutation& mutation,
                              MutationId dependsOn) override
    {
        SystemMutation m = mutation;
        m.dependencyId = static_cast<uint32_t>(dependsOn);
        m.flags |= MutationFlags::IS_DEPENDENT;
        m.sequenceNumber = sequenceCounter_.fetch_add(1, std::memory_order_relaxed);

        const bool pushed = queue_.push(m);

        if (pushed) {
            totalPushed_.fetch_add(1, std::memory_order_relaxed);
            updatePeakDepth();
            spaceCV_.notify_one();
        }

        return pushed;
    }

    //==========================================================================
    // Consumer: Pop a single mutation
    //==========================================================================

    bool popMutation(SystemMutation& outMutation) override
    {
        const bool popped = queue_.pop(outMutation);

        if (popped) {
            totalPopped_.fetch_add(1, std::memory_order_relaxed);
            spaceCV_.notify_one();
        }

        return popped;
    }

    //==========================================================================
    // Consumer: Pop multiple mutations
    //==========================================================================

    uint32_t popMultiple(SystemMutation* outMutations,
                         uint32_t maxMutations) override
    {
        const uint32_t popped = queue_.popMultiple(outMutations, maxMutations);
        totalPopped_.fetch_add(popped, std::memory_order_relaxed);
        if (popped > 0) {
            spaceCV_.notify_one();
        }
        return popped;
    }

    //==========================================================================
    // Factory: Create batch builder
    //==========================================================================

    std::unique_ptr<MutationBatchBuilder> createBatchBuilder() override
    {
        return std::make_unique<MutationBatchBuilderImpl>();
    }

    //==========================================================================
    // Query: Get queue depth
    //==========================================================================

    uint32_t getDepth() const override
    {
        return queue_.depth();
    }

    //==========================================================================
    // Query: Get queue capacity
    //==========================================================================

    uint32_t getCapacity() const override
    {
        return queue_.capacity();
    }

    //==========================================================================
    // Query: Get next sequence number
    //==========================================================================

    uint64_t getNextSequenceNumber() const override
    {
        return sequenceCounter_.load(std::memory_order_relaxed);
    }

    //==========================================================================
    // Statistics: Get current statistics
    //==========================================================================

    void getStatistics(Statistics& outStats) const override
    {
        outStats.totalPushed = totalPushed_.load(std::memory_order_relaxed);
        outStats.totalPopped = totalPopped_.load(std::memory_order_relaxed);
        outStats.droppedLowPriority = droppedLowPriority_.load(std::memory_order_relaxed);
        outStats.droppedCritical = droppedCritical_.load(std::memory_order_relaxed);
        outStats.batchPushes = batchPushes_.load(std::memory_order_relaxed);
        outStats.peakDepth = peakDepth_.load(std::memory_order_relaxed);
        outStats.currentDepth = queue_.depth();
    }

    //==========================================================================
    // Statistics: Reset counters
    //==========================================================================

    void resetStatistics() override
    {
        totalPushed_.store(0, std::memory_order_relaxed);
        totalPopped_.store(0, std::memory_order_relaxed);
        droppedLowPriority_.store(0, std::memory_order_relaxed);
        droppedCritical_.store(0, std::memory_order_relaxed);
        batchPushes_.store(0, std::memory_order_relaxed);
        peakDepth_.store(queue_.depth(), std::memory_order_relaxed);
    }

    //==========================================================================
    // Flow Control: Wait for space
    //==========================================================================

    bool waitForSpace(uint32_t timeoutMs) override
    {
        std::unique_lock<std::mutex> lock(spaceMutex_);

        // Check if space is already available
        if (!queue_.isFull()) {
            return true;
        }

        // Wait for space or timeout
        return spaceCV_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
            [this]() { return !queue_.isFull(); });
    }

    //==========================================================================
    // Configuration: Enable/disable priority dropping
    //==========================================================================

    void setPriorityDropping(bool enabled) override
    {
        priorityDroppingEnabled_.store(enabled, std::memory_order_relaxed);
    }

    //==========================================================================
    // Configuration: Check priority dropping status
    //==========================================================================

    bool isPriorityDroppingEnabled() const override
    {
        return priorityDroppingEnabled_.load(std::memory_order_relaxed);
    }
};

//==============================================================================
// Factory Implementation
//==============================================================================

std::unique_ptr<IMutationBridge> IMutationBridge::create(uint32_t capacity)
{
    // Validate capacity is power of 2
    if ((capacity & (capacity - 1)) != 0) {
        // Round up to next power of 2
        uint32_t powerOf2 = 1;
        while (powerOf2 < capacity) {
            powerOf2 <<= 1;
        }
        capacity = powerOf2;
    }

    // Clamp to reasonable range
    if (capacity < 8) capacity = 8;
    if (capacity > 16384) capacity = 16384;

    // Create instance based on capacity
    // Note: In production, this would use a switch or factory map
    // For now, we create a fixed-size instance
    #define CREATE_BRIDGE(cap) \
        case cap: return std::make_unique<MutationBridgeImpl<cap>>();

    // printf("Creating MutationBridge with capacity: %u\n", capacity);

    switch (capacity) {
        CREATE_BRIDGE(8);
        CREATE_BRIDGE(16);
        CREATE_BRIDGE(32);
        CREATE_BRIDGE(64);
        CREATE_BRIDGE(128);
        CREATE_BRIDGE(256);
        CREATE_BRIDGE(512);
        CREATE_BRIDGE(1024);
        CREATE_BRIDGE(2048);
        CREATE_BRIDGE(4096);
        CREATE_BRIDGE(8192);
        CREATE_BRIDGE(16384);
        default:
            // Default to 1024 if not exact match
            return std::make_unique<MutationBridgeImpl<1024>>();
    }

    #undef CREATE_BRIDGE
}

} // namespace Layer2
