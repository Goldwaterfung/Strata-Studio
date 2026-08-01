#pragma once

#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"

namespace DSP {

struct MidiSequencerState {
    TrackID trackId = TrackID::invalid();
    uint64_t lastExpectedSample = 0;
    bool hasLastExpected = false;

    void reset() {
        trackId = TrackID::invalid();
        lastExpectedSample = 0;
        hasLastExpected = false;
    }
};

class MidiSequencerFactory : public BaseNodeFactory<MidiSequencerState, 2048, NODE_TYPE_MIDI_SEQUENCER> {
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

void processMidiSequencer(
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
