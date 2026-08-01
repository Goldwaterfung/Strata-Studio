// clock_service.cpp
// Layer 2: Core Infrastructure Services - System Clock Service Implementation

#include "iclock_service.h"
#include <atomic>

#include <chrono>

namespace Layer2 {

class ClockServiceImpl : public IClockService {
public:
    ClockServiceImpl()
        : sampleRate(48000.0)
        , currentCycleId(0)
        , cycleStartTime(0)
        , cycleStartSteadyTime(0)
        , currentNumFrames(0)
        , nsPerSample(1e9 / 48000.0)
    {
    }

    void startCycle(uint64_t hardwareTimestamp, uint32_t numFrames) override {
        cycleStartTime.store(hardwareTimestamp, std::memory_order_release);
        currentNumFrames.store(numFrames, std::memory_order_release);
        currentCycleId.fetch_add(1, std::memory_order_acq_rel);

        uint64_t steadyNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count());
        cycleStartSteadyTime.store(steadyNs, std::memory_order_release);
    }

    void endCycle() override {
        // Any cleanup if needed
    }

    uint64_t getCycleStartTime() const override {
        return cycleStartTime.load(std::memory_order_acquire);
    }

    uint64_t getCycleId() const override {
        return currentCycleId.load(std::memory_order_acquire);
    }

    uint64_t getTimestampForSample(uint32_t sampleOffset) const override {
        uint64_t start = cycleStartTime.load(std::memory_order_acquire);
        return start + static_cast<uint64_t>(sampleOffset * nsPerSample);
    }

    uint32_t getOffsetForTimestamp(uint64_t rawTimestamp) const override {
        uint64_t start = cycleStartTime.load(std::memory_order_acquire);
        if (rawTimestamp <= start) return 0;
        
        uint64_t diff = rawTimestamp - start;
        uint32_t offset = static_cast<uint32_t>(diff / nsPerSample);
        
        uint32_t maxFrames = currentNumFrames.load(std::memory_order_acquire);
        return (offset < maxFrames) ? offset : (maxFrames - 1);
    }

    uint64_t getCycleStartSteadyTime() const override {
        return cycleStartSteadyTime.load(std::memory_order_acquire);
    }

    uint32_t getCurrentNumFrames() const override {
        return currentNumFrames.load(std::memory_order_acquire);
    }

    void setSampleRate(double newSampleRate) override {
        sampleRate = newSampleRate;
        if (sampleRate > 0) {
            nsPerSample = 1e9 / sampleRate;
        }
    }

    double getSampleRate() const override {
        return sampleRate;
    }

private:
    double sampleRate;
    std::atomic<uint64_t> currentCycleId;
    std::atomic<uint64_t> cycleStartTime;
    std::atomic<uint64_t> cycleStartSteadyTime;
    std::atomic<uint32_t> currentNumFrames;
    double nsPerSample;
};

std::unique_ptr<IClockService> IClockService::create() {
    return std::make_unique<ClockServiceImpl>();
}

} // namespace Layer2
