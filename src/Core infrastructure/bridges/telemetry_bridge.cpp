// telemetry_bridge.cpp v1.0
// Telemetry Bridge Implementation - Audio to GUI Thread Communication
//
// IMPLEMENTATION NOTES:
// - Uses MPSC lock-free queue for multi-producer support
// - Source filtering for efficient selective updates
// - Sequence numbering for gap detection
// - Statistics tracking for monitoring

#include "Core infrastructure/bridges/itelemetry_bridge.h"
#include "Core infrastructure/bridges/spsc_queue.h"
#include <atomic>
#include <cstring>
#include <unordered_set>
#include <mutex>
#include <shared_mutex>
#include <algorithm>
#include <vector>

namespace Layer2 {

//==============================================================================
// Internal Constants
//==============================================================================

namespace {
    // Maximum number of subscribed sources (prevents unbounded growth)
    constexpr uint32_t MAX_SUBSCRIBED_SOURCES = 1024;
}

//==============================================================================
// Subscription Manager (thread-safe, lock-free on the read path)
//==============================================================================

class SubscriptionManager {
private:
    std::atomic<uint32_t> subscribedGenerations_[65536];
    std::atomic<bool> subscribeAll_;

    // Mutex and set to protect updates from UI/NRT thread
    mutable std::mutex mutex_;
    std::unordered_set<uint64_t> subscribedSources_;

public:
    SubscriptionManager()
        : subscribeAll_(true)  // Default: subscribe to all
    {
        for (int i = 0; i < 65536; ++i) {
            subscribedGenerations_[i].store(0, std::memory_order_relaxed);
        }
    }

    //==========================================================================
    // Update subscriptions (NRT - UI Thread)
    //==========================================================================

    void setSubscriptions(const NodeID* sourceIds, uint32_t count)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Clear all atomic generations
        for (int i = 0; i < 65536; ++i) {
            subscribedGenerations_[i].store(0, std::memory_order_release);
        }
        subscribedSources_.clear();

        if (sourceIds == nullptr || count == 0) {
            subscribeAll_.store(true, std::memory_order_release);
        } else {
            subscribeAll_.store(false, std::memory_order_release);
            for (uint32_t i = 0; i < std::min(count, MAX_SUBSCRIBED_SOURCES); ++i) {
                NodeID id = sourceIds[i];
                if (id.id < 65536) {
                    subscribedGenerations_[id.id].store(id.generation, std::memory_order_release);
                    subscribedSources_.insert(id.toRaw());
                }
            }
        }
    }

    void addSource(NodeID sourceId)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribeAll_.store(false, std::memory_order_release);
        uint32_t slotIdx = sourceId.generation % 65536;
        if (slotIdx < 65536) {
            subscribedGenerations_[slotIdx].store(sourceId.generation, std::memory_order_release);
            subscribedSources_.insert(sourceId.toRaw());
        }
    }

    void removeSource(NodeID sourceId)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        uint32_t slotIdx = sourceId.generation % 65536;
        if (slotIdx < 65536) {
            subscribedGenerations_[slotIdx].store(0, std::memory_order_release);
            subscribedSources_.erase(sourceId.toRaw());
        }

        if (subscribedSources_.empty()) {
            subscribeAll_.store(true, std::memory_order_release);
        }
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribeAll_.store(true, std::memory_order_release);
        for (int i = 0; i < 65536; ++i) {
            subscribedGenerations_[i].store(0, std::memory_order_release);
        }
        subscribedSources_.clear();
    }

    //==========================================================================
    // Query subscription status (RT Safe - wait-free)
    //==========================================================================

    bool isSubscribed(NodeID sourceId) const
    {
        if (subscribeAll_.load(std::memory_order_acquire)) {
            return true;
        }

        uint32_t slotIdx = sourceId.generation % 65536;
        if (slotIdx >= 65536) {
            return false;
        }

        return subscribedGenerations_[slotIdx].load(std::memory_order_acquire) == sourceId.generation;
    }

    uint32_t getSubscriptionCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (subscribeAll_.load(std::memory_order_relaxed)) {
            return 0;  // 0 means "all"
        }

        return static_cast<uint32_t>(subscribedSources_.size());
    }

    bool isSubscribeAllMode() const
    {
        return subscribeAll_.load(std::memory_order_acquire);
    }
};

//==============================================================================
// Telemetry Bridge Implementation
//==============================================================================

template<uint32_t Capacity>
class TelemetryBridgeImpl : public ITelemetryBridge {
private:
    // The underlying SPSC queue (bounded, power of 2)
    SPSCQueue<BridgeTelemetryFrame, Capacity> queue_;

    // Sequence number counter (atomic for thread-safe access)
    std::atomic<uint64_t> sequenceCounter_;

    // Subscription manager
    SubscriptionManager subscriptions_;

    // Statistics
    std::atomic<uint64_t> totalPushed_;
    std::atomic<uint64_t> totalPolled_;
    std::atomic<uint64_t> dropped_;
    std::atomic<uint64_t> filteredOut_;
    std::atomic<uint64_t> sequenceGaps_;
    std::atomic<uint32_t> peakDepth_;

    // Active source tracking (lock-free on RT thread)
    std::atomic<bool> activeSourcesBitset_[65536];

    // Configuration
    std::atomic<bool> droppingEnabled_;

    // Overwrite-by-default atomic state arrays for meters
    alignas(64) std::atomic<float> latestPeaks_[65536][2];
    alignas(64) std::atomic<float> latestRms_[65536][2];
    alignas(64) std::atomic<bool> latestClipped_[65536][2];
    alignas(64) std::atomic<uint64_t> peakSequence_[65536][2];
    alignas(64) std::atomic<uint64_t> rmsSequence_[65536][2];
    alignas(64) std::atomic<bool> nodeHasPendingData_[65536];
    alignas(64) std::atomic<uint64_t> nodeIds_[65536];
    uint32_t lastCheckedNodeId_{0};

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

    //==========================================================================
    // Helper: Track active source (RT safe - wait-free)
    //==========================================================================

    void trackActiveSource(NodeID sourceId)
    {
        uint32_t slotIdx = sourceId.generation % 65536;
        if (slotIdx < 65536) {
            activeSourcesBitset_[slotIdx].store(true, std::memory_order_relaxed);
        }
    }

public:
    //==========================================================================
    // Constructor
    //==========================================================================

    TelemetryBridgeImpl()
        : sequenceCounter_(1)
        , totalPushed_(0)
        , totalPolled_(0)
        , dropped_(0)
        , filteredOut_(0)
        , sequenceGaps_(0)
        , peakDepth_(0)
        , droppingEnabled_(false)
        , lastCheckedNodeId_(0)
    {
        for (int i = 0; i < 65536; ++i) {
            activeSourcesBitset_[i].store(false, std::memory_order_relaxed);
            nodeIds_[i].store(0, std::memory_order_relaxed);
            latestPeaks_[i][0].store(0.0f, std::memory_order_relaxed);
            latestPeaks_[i][1].store(0.0f, std::memory_order_relaxed);
            latestRms_[i][0].store(0.0f, std::memory_order_relaxed);
            latestRms_[i][1].store(0.0f, std::memory_order_relaxed);
            latestClipped_[i][0].store(false, std::memory_order_relaxed);
            latestClipped_[i][1].store(false, std::memory_order_relaxed);
            peakSequence_[i][0].store(0, std::memory_order_relaxed);
            peakSequence_[i][1].store(0, std::memory_order_relaxed);
            rmsSequence_[i][0].store(0, std::memory_order_relaxed);
            rmsSequence_[i][1].store(0, std::memory_order_relaxed);
            nodeHasPendingData_[i].store(false, std::memory_order_relaxed);
        }
    }

    //==========================================================================
    // Producer: Push telemetry frame
    //==========================================================================

    bool pushTelemetry(const BridgeTelemetryFrame& frame) override
    {
        BridgeTelemetryFrame f = frame;

        // Check source subscription (lock-free)
        if (!subscriptions_.isSubscribed(f.sourceId)) {
            filteredOut_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        // Track active source (lock-free)
        trackActiveSource(f.sourceId);

        // Overwrite-by-default for Peak and RMS meters to bypass the queue
        if (f.type == TelemetryFrame::PEAK_METER || f.type == TelemetryFrame::RMS_METER) {
            uint32_t nodeId = f.sourceId.generation % 65536;
            if (nodeId < 65536) {
                uint64_t seq = sequenceCounter_.fetch_add(1, std::memory_order_relaxed);
                nodeIds_[nodeId].store(f.sourceId.toRaw(), std::memory_order_release);
                uint32_t channel = f.payload[2];
                if (channel < 2) {
                    float val;
                    std::memcpy(&val, &f.payload[0], sizeof(float));
                    if (f.type == TelemetryFrame::PEAK_METER) {
                        latestPeaks_[nodeId][channel].store(val, std::memory_order_release);
                        latestClipped_[nodeId][channel].store(f.payload[1] != 0, std::memory_order_release);
                        peakSequence_[nodeId][channel].store(seq, std::memory_order_release);
                    } else {
                        latestRms_[nodeId][channel].store(val, std::memory_order_release);
                        rmsSequence_[nodeId][channel].store(seq, std::memory_order_release);
                    }
                    nodeHasPendingData_[nodeId].store(true, std::memory_order_release);
                }
            }
            totalPushed_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        // Assign sequence number
        f.sequenceNumber = sequenceCounter_.fetch_add(1, std::memory_order_relaxed);

        // Push to queue (returns false if pool is exhausted)
        if (queue_.push(f)) {
            totalPushed_.fetch_add(1, std::memory_order_relaxed);
            updatePeakDepth();
            return true;
        } else {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }

    //==========================================================================
    // Producer: Push telemetry with explicit parameters
    //==========================================================================

    bool pushTelemetry(NodeID sourceId,
                       uint8_t type,
                       const uint32_t* payload,
                       uint8_t priority) override
    {
        BridgeTelemetryFrame frame = {};
        frame.sourceId = sourceId;
        frame.type = type;
        frame.priority = priority;
        frame.flags = 0;

        // Copy payload
        if (payload != nullptr) {
            std::memcpy(frame.payload, payload, sizeof(frame.payload));
        }

        return pushTelemetry(frame);
    }

    //==========================================================================
    // Producer: Push telemetry with auto sequence
    //==========================================================================

    bool pushTelemetryAutoSeq(NodeID sourceId,
                             uint8_t type,
                             const uint32_t* payload,
                             uint8_t priority) override
    {
        return pushTelemetry(sourceId, type, payload, priority);
    }

    //==========================================================================
    // Consumer: Poll telemetry frames
    //==========================================================================

    uint32_t pollTelemetry(BridgeTelemetryFrame* outFrames,
                           uint32_t maxFrames,
                           uint64_t minSequenceNumber) override
    {
        uint64_t lastSeq = minSequenceNumber;
        uint32_t polled = 0;

        // Pop from queue
        BridgeTelemetryFrame temp;
        while (polled < maxFrames && queue_.pop(temp)) {
            if (temp.sequenceNumber >= minSequenceNumber) {
                outFrames[polled++] = temp;

                // Check for sequence gaps
                if (lastSeq > 0 && temp.sequenceNumber > lastSeq + 1) {
                    sequenceGaps_.fetch_add(1, std::memory_order_relaxed);
                }
                lastSeq = temp.sequenceNumber;
            }
        }

        totalPolled_.fetch_add(polled, std::memory_order_relaxed);
        return polled;
    }

    //==========================================================================
    // Consumer: Poll telemetry with source filtering
    //==========================================================================

    uint32_t pollTelemetryFiltered(BridgeTelemetryFrame* outFrames,
                                   uint32_t maxFrames) override
    {
        // If in "subscribe all" mode, use regular poll
        if (subscriptions_.isSubscribeAllMode()) {
            return pollTelemetry(outFrames, maxFrames, 0);
        }

        // Otherwise, poll and filter by subscription
        uint32_t polled = 0;
        uint64_t lastSeq = 0;

        // Pop from queue and filter
        BridgeTelemetryFrame temp;
        while (polled < maxFrames && queue_.pop(temp)) {
            if (subscriptions_.isSubscribed(temp.sourceId)) {
                outFrames[polled++] = temp;

                // Check for sequence gaps
                if (lastSeq > 0 && temp.sequenceNumber > lastSeq + 1) {
                    sequenceGaps_.fetch_add(1, std::memory_order_relaxed);
                }
                lastSeq = temp.sequenceNumber;
            } else {
                // Filtered out
                filteredOut_.fetch_add(1, std::memory_order_relaxed);
            }
        }

        totalPolled_.fetch_add(polled, std::memory_order_relaxed);
        return polled;
    }

    bool getLatestMeterValues(NodeID sourceId,
                              float& outPeakLeft, float& outPeakRight,
                              float& outRmsLeft, float& outRmsRight,
                              bool& outClipLeft, bool& outClipRight,
                              uint64_t* outSequence = nullptr) const override
    {
        uint32_t nodeId = sourceId.generation % 65536;
        if (nodeId < 65536) {
            uint64_t registeredId = nodeIds_[nodeId].load(std::memory_order_acquire);
            if (registeredId == sourceId.toRaw()) {
                outPeakLeft = latestPeaks_[nodeId][0].load(std::memory_order_acquire);
                outPeakRight = latestPeaks_[nodeId][1].load(std::memory_order_acquire);
                outRmsLeft = latestRms_[nodeId][0].load(std::memory_order_acquire);
                outRmsRight = latestRms_[nodeId][1].load(std::memory_order_acquire);
                outClipLeft = latestClipped_[nodeId][0].load(std::memory_order_acquire);
                outClipRight = latestClipped_[nodeId][1].load(std::memory_order_acquire);
                if (outSequence) {
                    uint64_t seq0 = peakSequence_[nodeId][0].load(std::memory_order_acquire);
                    uint64_t seq1 = peakSequence_[nodeId][1].load(std::memory_order_acquire);
                    *outSequence = std::max(seq0, seq1);
                }
                return true;
            }
        }
        return false;
    }

    //==========================================================================
    // Subscription Management
    //==========================================================================

    void setSubscribedSources(const NodeID* sourceIds, uint32_t count) override
    {
        subscriptions_.setSubscriptions(sourceIds, count);
    }

    void addSubscribedSource(NodeID sourceId) override
    {
        subscriptions_.addSource(sourceId);
    }

    void removeSubscribedSource(NodeID sourceId) override
    {
        subscriptions_.removeSource(sourceId);
    }

    void clearSubscriptions() override
    {
        subscriptions_.clear();
    }

    bool isSubscribed(NodeID sourceId) const override
    {
        return subscriptions_.isSubscribed(sourceId);
    }

    //==========================================================================
    // Query Operations
    //==========================================================================

    uint32_t getDepth() const override
    {
        return queue_.depth();
    }

    uint32_t getCapacity() const override
    {
        return Capacity;  // Return configured capacity
    }

    uint64_t getNextSequenceNumber() const override
    {
        return sequenceCounter_.load(std::memory_order_relaxed);
    }

    uint32_t getSubscriptionCount() const override
    {
        return subscriptions_.getSubscriptionCount();
    }

    //==========================================================================
    // Statistics
    //==========================================================================

    void getStatistics(Statistics& outStats) const override
    {
        outStats.totalPushed = totalPushed_.load(std::memory_order_relaxed);
        outStats.totalPolled = totalPolled_.load(std::memory_order_relaxed);
        outStats.dropped = dropped_.load(std::memory_order_relaxed);
        outStats.filteredOut = filteredOut_.load(std::memory_order_relaxed);
        outStats.sequenceGaps = sequenceGaps_.load(std::memory_order_relaxed);
        outStats.peakDepth = peakDepth_.load(std::memory_order_relaxed);
        outStats.currentDepth = queue_.depth();

        uint32_t activeCount = 0;
        for (int i = 0; i < 65536; ++i) {
            if (activeSourcesBitset_[i].load(std::memory_order_relaxed)) {
                activeCount++;
            }
        }
        outStats.activeSourceCount = activeCount;
    }

    void resetStatistics() override
    {
        totalPushed_.store(0, std::memory_order_relaxed);
        totalPolled_.store(0, std::memory_order_relaxed);
        dropped_.store(0, std::memory_order_relaxed);
        filteredOut_.store(0, std::memory_order_relaxed);
        sequenceGaps_.store(0, std::memory_order_relaxed);
        peakDepth_.store(queue_.depth(), std::memory_order_relaxed);

        for (int i = 0; i < 65536; ++i) {
            activeSourcesBitset_[i].store(false, std::memory_order_relaxed);
        }
    }

    //==========================================================================
    // Configuration
    //==========================================================================

    void setDroppingEnabled(bool enabled) override
    {
        droppingEnabled_.store(enabled, std::memory_order_relaxed);
    }

    bool isDroppingEnabled() const override
    {
        return droppingEnabled_.load(std::memory_order_relaxed);
    }
};

//==============================================================================
// Factory Implementation
//==============================================================================

std::unique_ptr<ITelemetryBridge> ITelemetryBridge::create(uint32_t capacity)
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
    // Note: For unbounded MPSCQueue, capacity is mainly for statistics
    #define CREATE_BRIDGE(cap) \
        case cap: return std::make_unique<TelemetryBridgeImpl<cap>>();

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
            return std::make_unique<TelemetryBridgeImpl<1024>>();
    }

    #undef CREATE_BRIDGE
}

} // namespace Layer2
