#pragma once

#include <cstdint>
#include <atomic>
#include "../system_primitives.h"

namespace Math { // Using Math namespace for DSP-related control logic as well, or should I use a new one?
                 // Actually, let's just use 'DSP' namespace if I'm in src/common/dsp.
}

namespace DSP {


/**
 * @brief Interface for monitoring parameter touch states and automation FSM decisions.
 * 
 * This component resolves the conflict between automation data and user input.
 */
class ITouchStateMonitor {
public:
    virtual ~ITouchStateMonitor() = default;

    /**
     * @brief Sets the sample rate for dynamic time-to-sample conversions.
     */
    virtual void setSampleRate(double sampleRate) = 0;

    /**
     * @brief Sets the automation mode for a specific parameter on a node.
     */
    virtual void setMode(NodeID nodeId, uint32_t paramIndex, AutomationMode mode) = 0;

    /**
     * @brief Updates the global transport state.
     */
    virtual void setTransportState(bool isPlaying) = 0;

    /**
     * @brief Signals whether the parameter is actively recording automation data right now.
     */
    virtual void setRecording(NodeID nodeId, uint32_t paramIndex, bool isRecording) = 0;

    /**
     * @brief Signals that a user has started or stopped touching a parameter.
     */
    virtual void setTouching(NodeID nodeId, uint32_t paramIndex, bool isTouching) = 0;

    /**
     * @brief Decision logic for the audio loop: Should we read the automation curve?
     */
    virtual bool shouldRead(NodeID nodeId, uint32_t paramIndex) const = 0;

    /**
     * @brief Decision logic for the audio loop: Should we record the current value as automation?
     */
    virtual bool shouldRecord(NodeID nodeId, uint32_t paramIndex) const = 0;

    /**
     * @brief Returns remaining samples for glide-back.
     */
    virtual uint32_t getGlideSamples(NodeID nodeId, uint32_t paramIndex) const = 0;

    /**
     * @brief Decrements the glide counter.
     */
    virtual void decrementGlide(NodeID nodeId, uint32_t paramIndex, uint32_t samples) = 0;

    /**
     * @brief Resets the "touched" or "latched" state for all parameters on a node.
     * Useful when the transport stops.
     */
    virtual void resetNodeState(NodeID nodeId) = 0;
};

/**
 * @brief Concrete implementation of the Automation FSM.
 * 
 * Uses a pre-allocated internal structure for O(1) real-time access.
 * Note: In a production environment, this might be a more complex sparse registry
 * if node/parameter counts are extremely high.
 */
class AutomationMonitor : public ITouchStateMonitor {
public:
    static constexpr uint32_t MAX_NODES = 1024;
    static constexpr uint32_t MAX_PARAMS_PER_NODE = 128;

    struct ParamState {
        std::atomic<AutomationMode> mode{AutomationMode::READ};
        std::atomic<bool> isTouching{false};
        std::atomic<bool> isLatched{false}; // For Latch mode
        std::atomic<bool> isRecording{false}; // Actively recording data
        std::atomic<uint32_t> glideSamplesRemaining{0}; // For Glide-back
    };

    void setSampleRate(double sampleRate) override {
        sampleRate_.store(sampleRate, std::memory_order_relaxed);
    }

    void setMode(NodeID nodeId, uint32_t paramIndex, AutomationMode mode) override {
        if (auto* state = getParamState(nodeId, paramIndex)) {
            state->mode.store(mode, std::memory_order_relaxed);
            // Reset latching when mode changes
            state->isLatched.store(false, std::memory_order_relaxed);
        }
    }

    void setTouching(NodeID nodeId, uint32_t paramIndex, bool isTouching) override {
        if (auto* state = getParamState(nodeId, paramIndex)) {
            bool wasTouching = state->isTouching.load(std::memory_order_relaxed);
            state->isTouching.store(isTouching, std::memory_order_relaxed);
            if (isTouching) {
                state->isLatched.store(true, std::memory_order_relaxed);
                state->glideSamplesRemaining.store(0, std::memory_order_relaxed);
            } else if (wasTouching && state->mode.load(std::memory_order_relaxed) == AutomationMode::TOUCH) {
                // Trigger 50ms glide-back dynamically based on sample rate
                double rate = sampleRate_.load(std::memory_order_relaxed);
                state->glideSamplesRemaining.store(static_cast<uint32_t>(0.05 * rate), std::memory_order_relaxed);
            }
        }
    }

    void setRecording(NodeID nodeId, uint32_t paramIndex, bool isRecording) override {
        if (auto* state = getParamState(nodeId, paramIndex)) {
            state->isRecording.store(isRecording, std::memory_order_relaxed);
        }
    }

    void setTransportState(bool isPlaying) override {
        isPlaying_.store(isPlaying, std::memory_order_relaxed);
        if (!isPlaying) {
            // Reset latching and gliding when transport stops
            for (uint32_t n = 0; n < MAX_NODES; ++n) {
                for (uint32_t p = 0; p < MAX_PARAMS_PER_NODE; ++p) {
                    _registry[n][p].isLatched.store(false, std::memory_order_relaxed);
                    _registry[n][p].isRecording.store(false, std::memory_order_relaxed);
                    _registry[n][p].glideSamplesRemaining.store(0, std::memory_order_relaxed);
                }
            }
        }
    }

    bool shouldRead(NodeID nodeId, uint32_t paramIndex) const override {
        const auto* state = getParamState(nodeId, paramIndex);
        if (!state) return true;

        AutomationMode mode = state->mode.load(std::memory_order_relaxed);
        bool isTouching = state->isTouching.load(std::memory_order_relaxed);
        bool isLatched = state->isLatched.load(std::memory_order_relaxed);
        bool isGliding = state->glideSamplesRemaining.load(std::memory_order_relaxed) > 0;
        bool isPlaying = isPlaying_.load(std::memory_order_relaxed);
        bool isRecording = state->isRecording.load(std::memory_order_relaxed);

        // If the transport is STOPPED, or if we are NOT actually recording right now,
        // we ALWAYS read the underlying automation curve. This allows the fader to snap to the
        // correct starting value when the user seeks the playhead, and ensures Write/Latch/Touch
        // modes read the curve during standard playback (not actively recording).
        // EXCEPT if the user is physically touching the fader right now (which overrides the read).
        if ((!isPlaying || !isRecording) && !isTouching) {
            return mode != AutomationMode::OFF;
        }

        switch (mode) {
            case AutomationMode::OFF:   return false;
            case AutomationMode::READ:  return true;
            case AutomationMode::TOUCH: return !isTouching || isGliding;
            case AutomationMode::LATCH: return !isLatched;
            case AutomationMode::WRITE: return false;
            case AutomationMode::TRIM:  return !isTouching || isGliding; // TRIM acts like TOUCH for FSM read decisions
            default: return true;
        }
    }

    bool shouldRecord(NodeID nodeId, uint32_t paramIndex) const override {
        const auto* state = getParamState(nodeId, paramIndex);
        if (!state) return false;

        AutomationMode mode = state->mode.load(std::memory_order_relaxed);
        bool isTouching = state->isTouching.load(std::memory_order_relaxed);
        bool isLatched = state->isLatched.load(std::memory_order_relaxed);

        switch (mode) {
            case AutomationMode::OFF:   return false;
            case AutomationMode::READ:  return false;
            case AutomationMode::TOUCH: return isTouching;
            case AutomationMode::LATCH: return isLatched;
            case AutomationMode::WRITE: return true;
            case AutomationMode::TRIM:  return isTouching; // TRIM records during touch
            default: return false;
        }
    }

    uint32_t getGlideSamples(NodeID nodeId, uint32_t paramIndex) const override {
        const auto* state = getParamState(nodeId, paramIndex);
        return state ? state->glideSamplesRemaining.load(std::memory_order_relaxed) : 0;
    }

    void decrementGlide(NodeID nodeId, uint32_t paramIndex, uint32_t samples) override {
        if (auto* state = getParamState(nodeId, paramIndex)) {
            uint32_t current = state->glideSamplesRemaining.load(std::memory_order_relaxed);
            uint32_t next = (current > samples) ? (current - samples) : 0;
            state->glideSamplesRemaining.store(next, std::memory_order_relaxed);
        }
    }

    void resetNodeState(NodeID nodeId) override {
        uint32_t nodeIdx = nodeId.index();
        if (nodeIdx >= MAX_NODES) return;

        for (uint32_t i = 0; i < MAX_PARAMS_PER_NODE; ++i) {
            _registry[nodeIdx][i].isLatched.store(false, std::memory_order_relaxed);
            _registry[nodeIdx][i].isTouching.store(false, std::memory_order_relaxed);
            _registry[nodeIdx][i].glideSamplesRemaining.store(0, std::memory_order_relaxed);
        }
    }

private:
    ParamState* getParamState(NodeID nodeId, uint32_t paramIndex) {
        uint32_t nodeIdx = nodeId.index();
        if (nodeIdx >= MAX_NODES || paramIndex >= MAX_PARAMS_PER_NODE) return nullptr;
        return &_registry[nodeIdx][paramIndex];
    }

    const ParamState* getParamState(NodeID nodeId, uint32_t paramIndex) const {
        uint32_t nodeIdx = nodeId.index();
        if (nodeIdx >= MAX_NODES || paramIndex >= MAX_PARAMS_PER_NODE) return nullptr;
        return &_registry[nodeIdx][paramIndex];
    }

    // Pre-allocated registry for all nodes and parameters
    // This is roughly 1024 * 128 * sizeof(ParamState) bytes.
    // ParamState is small (a few atoms).
    std::atomic<double> sampleRate_{48000.0};
    std::atomic<bool> isPlaying_{false};
    ParamState _registry[MAX_NODES][MAX_PARAMS_PER_NODE];
};

} // namespace DSP
