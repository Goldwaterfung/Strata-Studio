// event_queue.cpp v1.0
// Event Queue Implementation
#include "Core infrastructure/bridges/ievent_queue.h"
#include "Core infrastructure/bridges/spsc_queue.h"
#include <atomic>

namespace Layer2 {

class EventQueueImpl : public IEventQueue {
private:
    SPSCQueue<EventData, 1024> queue_;
    std::atomic<bool> droppingEnabled_;
    uint32_t capacity_;

    // Phase 1 Synchronization
    std::atomic<uint32_t> latchedWriteIndex_;

public:
    EventQueueImpl(const Config& config)
        : droppingEnabled_(config.dropOldestWhenFull)
        , capacity_(1024)
        , latchedWriteIndex_(0)
    {}

    void prepareCycle() override {
        latchedWriteIndex_.store(queue_.getWriteIndex(), std::memory_order_release);
    }

    bool popEventForCycle(EventData& outEvent) override {
        return queue_.popLimited(outEvent, latchedWriteIndex_.load(std::memory_order_acquire));
    }

    bool pushEvent(const EventData& event) override {
        return queue_.push(event);
    }

    bool pushEventWithPriority(const EventData& event, uint8_t priority) override {
        (void)priority;
        return queue_.push(event);
    }

    uint32_t pushBatch(const EventData* events, uint32_t count) override {
        return queue_.tryPushBatch(events, count);
    }

    uint32_t popMultiple(EventData* outEvents, uint32_t maxEvents, uint32_t numSamples = 1024) override {
        (void)numSamples;
        return queue_.popMultiple(outEvents, maxEvents);
    }

    uint32_t popMultipleWithSequence(EventData* outEvents,
                                     uint32_t maxEvents,
                                     uint64_t minSequenceNumber) override {
        (void)minSequenceNumber;
        return queue_.popMultiple(outEvents, maxEvents);
    }

    uint32_t getDepth() const override {
        return queue_.depth();
    }

    uint32_t getCapacity() const override {
        return capacity_;
    }

    uint64_t getNextSequenceNumber() const override {
        return 0;
    }

    void getStatistics(Statistics& outStats) const override {
        outStats = {};
    }

    void resetStatistics() override {}

    void setDroppingEnabled(bool enabled) override {
        droppingEnabled_.store(enabled);
    }

    bool isDroppingEnabled() const override {
        return droppingEnabled_.load();
    }
};

std::unique_ptr<IEventQueue> IEventQueue::create(const Config& config) {
    return std::make_unique<EventQueueImpl>(config);
}

} // namespace Layer2
