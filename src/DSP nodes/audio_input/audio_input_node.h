#pragma once

#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"
#include "Core infrastructure/bridges/spsc_queue.h"
#include <cstring>

namespace DSP {

struct AudioInputState {
    struct InnerState {
        bool isMonitoringActive;
        bool isRecordArmed;
        uint8_t hardwareChannelIndex;
        uint8_t numChannels;
        uint32_t padding;
        std::atomic<uint64_t>* actualStartSample;
        Layer2::SPSCQueue<float, 524288>* recordingQueue;
        Layer2::SPSCQueue<float, 16384>* waveformQueue;
    } buffers[2];

    void reset() {
        buffers[0].isMonitoringActive = false;
        buffers[0].isRecordArmed = false;
        buffers[0].hardwareChannelIndex = 0;
        buffers[0].numChannels = 1;
        buffers[0].padding = 0;
        buffers[0].actualStartSample = nullptr;
        buffers[0].recordingQueue = nullptr;
        buffers[0].waveformQueue = nullptr;
        buffers[1] = buffers[0];
    }
};

static_assert(std::is_pod<AudioInputState>::value, "AudioInputState must remain a POD type");

// processAudioInput():
//   Copies context->inputChannels[hardwareChannelIndex .. +numChannels]
//   into outputs[], numSamples frames. RT-safe: no allocation, bounded loop.
//   Guards against out-of-bounds hardware channel access by checking against context->numInputChannels.
void processAudioInput(
    NodeID nodeId, float* const* inputs, float* const* outputs,
    uint32_t numChannels, uint32_t numSamples,
    const EventData* events, uint32_t numEvents, EventData* outEvents, uint32_t* outEventCount,
    const ProcessContext* context, const bool* inputSilence, bool* isOutputSilent);

class AudioInputFactory
    : public BaseNodeFactory<AudioInputState, 64, NODE_TYPE_AUDIO_INPUT> {
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

} // namespace DSP
