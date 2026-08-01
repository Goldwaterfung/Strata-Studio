#pragma once

#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"

#include "common/math/smoothing.h"

namespace DSP {

/**
 * @brief Insert Plugin DSP Node State (POD)
 * 
 * Wrapper for hosting external plugins (VST3, AU, etc.).
 */
struct InsertPluginState {
    PluginHandle pluginHandle = PluginHandle::invalid();
    void* pluginInstance = nullptr; // Opaque pointer to the host-side plugin wrapper
    bool bypass = false;
    Math::LinearRamp bypassRamp;
    char name[MAX_PLUGIN_NAME_LENGTH];
    uint32_t tailSamplesRemaining = 0;

    void reset() {
        pluginHandle = PluginHandle::invalid();
        pluginInstance = nullptr;
        bypass = false;
        bypassRamp.init(1.0f, 512); // ~11ms at 44.1k
        std::memset(name, 0, sizeof(name));
        tailSamplesRemaining = 0;
    }
};

/**
 * @brief Factory for creating Insert Plugin nodes.
 */
class InsertPluginFactory : public BaseNodeFactory<InsertPluginState, 2048, NODE_TYPE_INSERT_PLUGIN> {
public:
    NodeID createNode() override {
        auto id = BaseNodeFactory::createNode();
        if (id.isValid()) {
            if (auto* state = getRegistry().get(id)) {
                state->reset();
            }
        }
        return id;
    }
};

/**
 * @brief Standardized processing function for the Insert Plugin wrapper.
 */
void processInsertPlugin(
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
