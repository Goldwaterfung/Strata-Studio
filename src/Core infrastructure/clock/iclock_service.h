// iclock_service.h
// Layer 2: Core Infrastructure Services - System Clock Service Interface
// Provides deterministic time referencing for the 3-phase pipeline

#pragma once

#include "system_primitives.h"
#include <cstdint>
#include <memory>

namespace Layer2 {

class IClockService {
public:
    virtual ~IClockService() = default;

    //==========================================================================
    // Cycle Management (Called by Layer 3 Audio Engine)
    //==========================================================================

    /**
     * @brief Latch the current cycle's time reference (Phase 1)
     * @param hardwareTimestamp The raw high-res timestamp from the audio driver
     * @param numFrames Number of frames in this cycle
     * @thread_safety RT-Safe, Wait-Free
     */
    virtual void startCycle(uint64_t hardwareTimestamp, uint32_t numFrames) = 0;

    /**
     * @brief End the current cycle (Phase 3)
     * @thread_safety RT-Safe, Wait-Free
     */
    virtual void endCycle() = 0;

    //==========================================================================
    // Time Queries (RT-Safe, Wait-Free)
    //==========================================================================

    /**
     * @brief Get the high-res timestamp latched at the start of the current cycle
     */
    virtual uint64_t getCycleStartTime() const = 0;

    /**
     * @brief Get the monotonic ID of the current cycle
     */
    virtual uint64_t getCycleId() const = 0;

    /**
     * @brief Get the estimated time (in nanoseconds) for a sample within the current buffer
     * @param sampleOffset Offset from 0 to numFrames-1
     */
    virtual uint64_t getTimestampForSample(uint32_t sampleOffset) const = 0;

    /**
     * @brief Convert a raw system timestamp to a sample-accurate offset within the current cycle
     * @param rawTimestamp The external timestamp to convert (e.g. from MIDI)
     * @return Sample offset within current cycle, or 0 if timestamp is in the past
     */
    virtual uint32_t getOffsetForTimestamp(uint64_t rawTimestamp) const = 0;

    /**
     * @brief Get the steady clock time (in nanoseconds) latched at the start of the current cycle
     */
    virtual uint64_t getCycleStartSteadyTime() const = 0;

    /**
     * @brief Get the frame count of the current cycle
     */
    virtual uint32_t getCurrentNumFrames() const = 0;

    //==========================================================================
    // Configuration (Non-RT-Safe)
    //==========================================================================

    virtual void setSampleRate(double sampleRate) = 0;
    virtual double getSampleRate() const = 0;

    //==========================================================================
    // Factory
    //==========================================================================

    static std::unique_ptr<IClockService> create();
};

} // namespace Layer2
