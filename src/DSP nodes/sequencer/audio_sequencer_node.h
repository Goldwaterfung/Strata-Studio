#pragma once

#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"

namespace Layer3 { 
    class IButlerThread; 
    class IStreamingBuffer;
}

namespace Layer1 {
    class IFileSystem;
}

namespace DSP {

struct AudioSequencerState {
    TrackID trackId = TrackID::invalid();
    float targetGain = 1.0f;
    Layer3::IStreamingBuffer* buffers[MAX_BUFFERS_PER_TRACK] = {nullptr};

    void reset() {
        trackId = TrackID::invalid();
        targetGain = 1.0f;
        for (uint32_t i = 0; i < MAX_BUFFERS_PER_TRACK; ++i) {
            buffers[i] = nullptr;
        }
    }
};

class AudioSequencerFactory : public BaseNodeFactory<AudioSequencerState, 2048, NODE_TYPE_AUDIO_SEQUENCER> {
public:
    static Layer3::IButlerThread* s_butlerThread;
    static void setButlerThread(Layer3::IButlerThread* butler) { s_butlerThread = butler; }

    static Layer1::IFileSystem* s_fileSystem;
    static void setFileSystem(Layer1::IFileSystem* fs) { s_fileSystem = fs; }

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

void processAudioSequencer(
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
