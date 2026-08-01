#pragma once

#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"
#include "common/math/smoothing.h"

#include <atomic>

namespace DSP {

class ITouchStateMonitor;

void setPannerAutomationMonitor(ITouchStateMonitor* monitor);

/**
 * @brief Panner DSP Node State (POD)
 * 
 * Supports Stereo Balance and Equal Power Panning with Width control.
 */
struct PannerState {
    // Parameters
    std::atomic<float> targetPan{0.5f};   // 0.5 is center
    std::atomic<float> targetWidth{1.0f}; // 1.0 is normal stereo
    uint32_t mode = 0;        // 0: Equal Power, 1: Linear Balance

    // Smoothers
    Math::ParameterSmoother panSmoother;
    Math::ParameterSmoother widthSmoother;

    void reset(float sampleRate) {
        targetPan.store(0.5f, std::memory_order_relaxed);
        targetWidth.store(1.0f, std::memory_order_relaxed);
        mode = 0;
        
        panSmoother.init(0.5f, 10.0f, sampleRate);
        widthSmoother.init(1.0f, 10.0f, sampleRate);
    }
};

/**
 * @brief Factory for creating Panner nodes.
 */
class PannerFactory : public BaseNodeFactory<PannerState, 1024, NODE_TYPE_PANNER> {
public:
    NodeID createNode() override {
        auto id = BaseNodeFactory::createNode();
        if (id.isValid()) {
            if (auto* state = getRegistry().get(id)) {
                state->reset(44100.0f);
            }
        }
        return id;
    }
};

/**
 * @brief Standardized processing function for the Panner.
 */
void processPanner(
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
