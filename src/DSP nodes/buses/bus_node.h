#pragma once

#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"
#include "common/math/smoothing.h"

#include <atomic>

namespace DSP {

class ITouchStateMonitor;

void setBusAutomationMonitor(ITouchStateMonitor* monitor);

/**
 * @brief Bus / Group DSP Node State (POD)
 * 
 * Provides a master summing point with gain control.
 */
struct BusState {
    std::atomic<float> targetGain{1.0f};
    Math::ParameterSmoother gainSmoother;

    void reset(float sampleRate) {
        targetGain.store(1.0f, std::memory_order_relaxed);
        gainSmoother.init(1.0f, 10.0f, sampleRate);
    }
};

/**
 * @brief Factory for creating Bus nodes.
 */
class BusFactory : public BaseNodeFactory<BusState, 512, NODE_TYPE_BUS> {
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
 * @brief Standardized processing function for the Bus node.
 */
void processBus(
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
