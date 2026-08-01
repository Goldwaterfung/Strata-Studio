#pragma once

#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"
#include <cstring>

namespace DSP {

/**
 * @brief Plugin Slot DSP Node State (POD)
 * 
 * Container hosting up to 8 InsertPluginNodes in series.
 */
struct PluginSlotState {
    static constexpr uint32_t MAX_SLOTS = 8;
    static constexpr uint32_t MAX_EVENTS_POOL = 512; // Pre-allocated max block events
    NodeID slots[MAX_SLOTS];
    bool bypass[MAX_SLOTS];

    // Ping-pong double buffers for real-time safe MIDI event chaining
    EventData scratchBufferA[MAX_EVENTS_POOL];
    EventData scratchBufferB[MAX_EVENTS_POOL];

    void reset() {
        for (uint32_t i = 0; i < MAX_SLOTS; ++i) {
            slots[i] = NodeID::invalid();
            bypass[i] = false;
        }
        std::memset(scratchBufferA, 0, sizeof(scratchBufferA));
        std::memset(scratchBufferB, 0, sizeof(scratchBufferB));
    }
};

/**
 * @brief Factory for creating Plugin Slot nodes.
 */
class PluginSlotFactory : public BaseNodeFactory<PluginSlotState, 1024, NODE_TYPE_PLUGIN_SLOT> {
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
 * @brief Standardized processing function for the Plugin Slot container.
 */
void processPluginSlot(
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

/**
 * @brief Core processing function for Plugin Slot state.
 * Use this when composing PluginSlotState inside macro-nodes.
 */
void processPluginSlotState(
    PluginSlotState* s,
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
