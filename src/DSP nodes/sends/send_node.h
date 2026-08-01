#pragma once

#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"
#include "common/math/smoothing.h"

#include <atomic>

namespace DSP {

class ITouchStateMonitor;

void setSendAutomationMonitor(ITouchStateMonitor* monitor);

/**
 * @brief Send DSP Node State (POD)
 * 
 * Provides an auxiliary signal tap with gain control.
 */
struct SendState {
    std::atomic<float> targetGain{0.0f}; // Defaults to -inf (0.0 linear)
    Math::ParameterSmoother gainSmoother;

    void reset(float sampleRate) {
        targetGain.store(0.0f, std::memory_order_relaxed);
        gainSmoother.init(0.0f, 10.0f, sampleRate);
    }
};

/**
 * @brief Factory for creating Send nodes.
 */
class SendFactory : public BaseNodeFactory<SendState, 1024, NODE_TYPE_SEND> {
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
 * @brief Standardized processing function for the Send node.
 */
void processSend(
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
