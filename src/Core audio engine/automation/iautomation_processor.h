// src/Core audio engine/automation/iautomation_processor.h
#pragma once

#include "system_primitives.h"
#include <cstdint>
#include <memory>

namespace Layer3 {

//==============================================================================
// AUTOMATION PROCESSOR INTERFACE
//==============================================================================

class IAutomationProcessor {
public:
    //==========================================================================
    // Factory
    //==========================================================================

    static std::unique_ptr<IAutomationProcessor> create();

    //==========================================================================
    // Automation Generation (RT-Safe)
    //==========================================================================

    // Generate automation events for current buffer
    // Parameters:
    //   startPosition: Start position in samples
    //   numSamples: Number of samples in buffer
    //   outEvents: Output event array
    //   maxEvents: Maximum events to generate
    //   isPlaying: True if transport is currently playing
    // Returns: Number of events generated
    // Thread-safety: RT-safe, wait-free (no allocations)
    virtual uint32_t generateAutomationEvents(
        uint64_t startPosition,
        uint32_t numSamples,
        EventData* outEvents,
        uint32_t maxEvents,
        bool isPlaying
    ) = 0;

    //==========================================================================
    // Automation Recording (RT-Safe)
    //==========================================================================

    // Record automation value at position
    // Parameters:
    //   targetNodeId: Target node identifier
    //   subNodeId: Target sub-node identifier
    //   parameterIndex: Parameter index within the node
    //   value: Value to record
    //   position: Position in samples
    // Thread-safety: RT-safe, wait-free (atomic write)
    virtual void recordAutomationValue(
        NodeID targetNodeId,
        uint32_t subNodeId,
        uint32_t parameterIndex,
        float value,
        uint64_t position
    ) = 0;

    //==========================================================================
    // Playback Updates (Non-RT)
    //==========================================================================

    // Update playback points (NRT-safe, wait-free swap)
    virtual void updatePlaybackPoints(
        NodeID targetNodeId,
        uint32_t subNodeId,
        uint32_t parameterIndex,
        const ::AutomationPoint* points,
        uint32_t count
    ) = 0;

    //==========================================================================
    // Destructor
    //==========================================================================

    virtual ~IAutomationProcessor() = default;
};

} // namespace Layer3
