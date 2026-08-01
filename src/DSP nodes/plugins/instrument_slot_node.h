#pragma once

#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"
#include "common/math/smoothing.h"

#include "DSP nodes/sine_synth/sine_synth_node.h"

namespace DSP {

/**
 * @brief Instrument Slot DSP Node State (POD)
 * 
 * Wrapper for hosting virtual instrument plugins (Synths, Samplers, VSTi, AU).
 */
struct InstrumentSlotState {
    PluginHandle pluginHandle;
    void* pluginInstance;           // Opaque pointer to the host-side plugin wrapper
    bool bypass;
    bool acceptLiveMIDI;            // Store NRT MIDI monitoring state
    uint8_t reserved[6];            // Maintain 8-byte alignment
    Math::LinearRamp bypassRamp;
    char name[MAX_PLUGIN_NAME_LENGTH];
    SineSynthState fallbackSynth;
    EventData rtScratchEvents[512]; // Persistent scratch events to prevent stack frames overflow

    void reset() {
        pluginHandle = PluginHandle::invalid();
        pluginInstance = nullptr;
        bypass = false;
        acceptLiveMIDI = true;
        std::memset(reserved, 0, sizeof(reserved));
        bypassRamp.init(1.0f, 512); // ~11ms at 44.1k
        std::memset(name, 0, sizeof(name));
        fallbackSynth.reset();
        std::memset(rtScratchEvents, 0, sizeof(rtScratchEvents));
    }
};

static_assert(std::is_standard_layout<InstrumentSlotState>::value, "InstrumentSlotState must have standard layout");

/**
 * @brief Factory for creating Instrument Slot nodes.
 */
class InstrumentSlotFactory : public BaseNodeFactory<InstrumentSlotState, 512, NODE_TYPE_INSTRUMENT_SLOT> {
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
 * @brief Standardized processing function for the Instrument Slot wrapper.
 */
void processInstrumentSlot(
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
 * @brief Core processing function for Instrument Slot state.
 * Use this when composing InstrumentSlotState inside macro-nodes.
 */
void processInstrumentSlotState(
    InstrumentSlotState* s,
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

#include "DSP nodes/tracks/instrument_track_node.h"
