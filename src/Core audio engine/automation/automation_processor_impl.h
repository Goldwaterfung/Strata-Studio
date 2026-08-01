// src/Core audio engine/automation/automation_processor_impl.h
#pragma once

#include "iautomation_processor.h"
#include "spsc_queue.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "common/math/gain.h"
#include "DSP nodes/channelstrip/channel_strip_node.h"

namespace Layer3 {

//==============================================================================
// AUTOMATION LANE (Internal State)
//==============================================================================

class AutomationLane {
public:
    //==========================================================================
    // Construction
    //==========================================================================

    explicit AutomationLane(NodeID targetNodeId, uint32_t subNodeId, uint32_t parameterIndex);
    ~AutomationLane();

    // Disable copy, enable move
    AutomationLane(const AutomationLane&) = delete;
    AutomationLane& operator=(const AutomationLane&) = delete;
    AutomationLane(AutomationLane&& other) noexcept;
    AutomationLane& operator=(AutomationLane&& other) noexcept;

    //==========================================================================
    // RT-Safe Operations
    //==========================================================================

    // Record automation value at position (RT-safe, wait-free)
    void recordValue(float value, uint64_t position);

    // Generate automation events for range (RT-safe, wait-free)
    uint32_t generateEvents(
        uint64_t startPosition,
        uint32_t numSamples,
        EventData* outEvents,
        uint32_t maxEvents,
        bool isPlaying
    ) const;

    //==========================================================================
    // Playback Updates (Non-RT)
    //==========================================================================

    // Update playback points (NRT-safe double-buffered swap)
    void updatePoints(const ::AutomationPoint* points, uint32_t count);

    //==========================================================================
    // Accessors
    //==========================================================================

    void setTarget(NodeID targetNodeId, uint32_t subNodeId, uint32_t parameterIndex) {
        targetNodeId_ = targetNodeId;
        subNodeId_ = subNodeId;
        parameterIndex_ = parameterIndex;
        if (targetNodeId.isValid()) {
            if (parameterIndex == static_cast<uint32_t>(DSP::ChannelStripParameter::Volume)) {
                // Fader: 0 dB normalized
                lastValue_.store(Math::Gain::UNITY_NORMALIZED, std::memory_order_relaxed);
            } else if (parameterIndex == static_cast<uint32_t>(DSP::ChannelStripParameter::Pan)) {
                // Pan: center
                lastValue_.store(Math::Gain::CENTER_PAN_NORMALIZED, std::memory_order_relaxed);
            } else if (parameterIndex == static_cast<uint32_t>(DSP::ChannelStripParameter::Mute) ||
                       parameterIndex == static_cast<uint32_t>(DSP::ChannelStripParameter::Solo)) {
                // Boolean toggles default to off
                lastValue_.store(0.0f, std::memory_order_relaxed);
            } else if (parameterIndex == 0) {
                // Panner Width (index 0 on panner node), Send Level (index 0 on send node),
                // or MonitorState (index 0 on monitor node) all default to 0.
                // Panner Pan (index 0 on panner) defaults to centre, but we cannot distinguish
                // node types here — keep 0.0f; the capture engine provides the real default.
                lastValue_.store(0.0f, std::memory_order_relaxed);
            } else {
                // All other parameters (plugin parameters, send pan, etc.) default off.
                lastValue_.store(0.0f, std::memory_order_relaxed);
            }
        }
    }

private:
    //==========================================================================
    // Internal State
    //==========================================================================

    NodeID targetNodeId_;
    uint32_t subNodeId_;
    uint32_t parameterIndex_;

    // Ring buffer for recorded automation values (pre-allocated)
    static constexpr uint32_t RECORDING_BUFFER_SIZE = 4096;
    using RecordingBuffer = Layer2::SPSCQueue<::AutomationPoint, RECORDING_BUFFER_SIZE>;
    std::unique_ptr<RecordingBuffer> recordingBuffer_;

    // Simple interpolation state
    mutable std::atomic<float> lastValue_;
    mutable std::atomic<uint64_t> lastPosition_;

    // Double-buffered playback points to prevent allocations and races
    static constexpr uint32_t MAX_PLAYBACK_POINTS = 1024;
    std::array<::AutomationPoint, MAX_PLAYBACK_POINTS> playbackPoints_[2];
    uint32_t playbackPointsCount_[2]{0, 0};
    std::atomic<uint32_t> activeBufferIndex_{0};
};

//==============================================================================
// AUTOMATION PROCESSOR IMPLEMENTATION
//==============================================================================

class AutomationProcessorImpl : public IAutomationProcessor {
public:
    //==========================================================================
    // Factory
    //==========================================================================

    static std::unique_ptr<IAutomationProcessor> create();

    //==========================================================================
    // Construction/Destruction
    //==========================================================================

    AutomationProcessorImpl();
    ~AutomationProcessorImpl() override;

    // Disable copy and move
    AutomationProcessorImpl(const AutomationProcessorImpl&) = delete;
    AutomationProcessorImpl& operator=(const AutomationProcessorImpl&) = delete;
    AutomationProcessorImpl(AutomationProcessorImpl&&) = delete;
    AutomationProcessorImpl& operator=(AutomationProcessorImpl&&) = delete;

    //==========================================================================
    // IAutomationProcessor Implementation
    //==========================================================================

    uint32_t generateAutomationEvents(
        uint64_t startPosition,
        uint32_t numSamples,
        EventData* outEvents,
        uint32_t maxEvents,
        bool isPlaying
    ) override;

    void recordAutomationValue(
        NodeID targetNodeId,
        uint32_t subNodeId,
        uint32_t parameterIndex,
        float value,
        uint64_t position
    ) override;

    void updatePlaybackPoints(
        NodeID targetNodeId,
        uint32_t subNodeId,
        uint32_t parameterIndex,
        const ::AutomationPoint* points,
        uint32_t count
    ) override;

private:
    //==========================================================================
    // Internal State
    //==========================================================================

    // Collection of automation lanes (indexed by parameterId)
    // FIXED: Using fixed-size array to prevent reallocation during RT operations
    struct LaneEntry {
        NodeID targetNodeId;
        uint32_t subNodeId;
        uint32_t parameterIndex;
        std::unique_ptr<AutomationLane> lane;
    };
    // IMPORTANT: MAX_LANES MUST match the AutomationSubLaneUIState subLanes[] array
    // size in itrack_controller.h (currently 128). The static_assert below will break
    // the build if either constant is changed without updating the other.
    static constexpr uint32_t MAX_LANES = 128;
    static_assert(MAX_LANES == 128,
                  "AutomationProcessorImpl::MAX_LANES must equal TrackUIState::subLanes[] capacity (128). "
                  "Update both itrack_controller.h and automation_processor_impl.h together.");
    std::array<LaneEntry, MAX_LANES> lanes_;

    // Atomic counter for active lanes (RT-safe read/write)
    std::atomic<uint32_t> laneCount_;

    // Mutex for lane management (non-RT only)
    std::mutex laneMutex_;

    //==========================================================================
    // Internal Helpers
    //==========================================================================

    // Find or create lane for parameter (now RT-safe due to fixed array)
    AutomationLane* findOrCreateLane(NodeID targetNodeId, uint32_t subNodeId, uint32_t parameterIndex);
};

} // namespace Layer3
