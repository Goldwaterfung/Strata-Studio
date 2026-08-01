#pragma once

#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"
#include "common/math/smoothing.h"

#include <atomic>

namespace DSP {

class ITouchStateMonitor;

void setChannelStripAutomationMonitor(ITouchStateMonitor* monitor);

/**
 * @brief Semantic identifiers for Channel Strip parameters.
 * Replaces hardcoded indices across the automation system.
 */
enum class ChannelStripParameter : uint32_t {
    Volume = 0,
    Pan = 1,
    Mute = 2,
    Solo = 3
};

/**
 * @brief Channel Strip DSP Node State (POD)
 * 
 * Handles gain, panning, mute, and solo with high-performance 1-pole smoothing.
 */
struct ChannelStripState {
    // Parameters
    std::atomic<float> targetGain{1.0f};
    std::atomic<float> targetPan{0.5f}; // 0.5 is center
    std::atomic<float> currentGain{1.0f};
    std::atomic<float> currentPan{0.5f};
    std::atomic<bool> mute{false};
    std::atomic<bool> solo{false};

    // Smoothers
    Math::ParameterSmoother gainSmoother;
    Math::ParameterSmoother panSmoother;
    
    // Ramps for binary states (64 samples linear)
    Math::LinearRamp muteRamp; // 1.0 = unmuted, 0.0 = muted
    Math::LinearRamp soloRamp;

    void reset(float sampleRate) {
        targetGain.store(1.0f, std::memory_order_relaxed);
        targetPan.store(0.5f, std::memory_order_relaxed);
        currentGain.store(1.0f, std::memory_order_relaxed);
        currentPan.store(0.5f, std::memory_order_relaxed);
        mute.store(false, std::memory_order_relaxed);
        solo.store(false, std::memory_order_relaxed);
        
        // Initialize smoothers with 10ms ramp
        gainSmoother.init(1.0f, 10.0f, sampleRate);
        panSmoother.init(0.5f, 10.0f, sampleRate);
        
        // Initialize binary ramps
        muteRamp.init(1.0f, 64);
        soloRamp.init(1.0f, 64);
    }
};

/**
 * @brief Factory for creating Channel Strip nodes.
 */
class ChannelStripFactory : public BaseNodeFactory<ChannelStripState, 1024, NODE_TYPE_CHANNEL_STRIP> {
public:
    NodeID createNode() override {
        // We need the sample rate to initialize the smoothers.
        // In a real DAW, the factory would get this from the kernel.
        // For now, we'll assume a default or use a specialized init method.
        auto id = BaseNodeFactory::createNode();
        if (id.isValid()) {
            if (auto* state = getRegistry().get(id)) {
                state->reset(44100.0f); // Default SR, should be updated by kernel
            }
        }
        return id;
    }
};

/**
 * @brief The standardized processing function for the Channel Strip.
 */
void processChannelStrip(
    NodeID nodeId,
    float* const* inputs,
    float* const* outputs,
    uint32_t numChannels,
    uint32_t numSamples,
    const EventData* events,
    uint32_t numEvents,
    EventData* outEvents,
    uint32_t* outEventCount,
    const ProcessContext* context,
    const bool* inputSilence,
    bool* isOutputSilent
);

} // namespace DSP
