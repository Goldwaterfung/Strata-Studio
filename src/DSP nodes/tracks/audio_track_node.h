#pragma once
#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"
#include "DSP nodes/channelstrip/channel_strip_node.h"
#include "DSP nodes/plugins/plugin_slot_node.h"
#include "DSP nodes/sends/send_node.h"
#include "DSP nodes/latency/latency_node.h"

namespace DSP {

struct AudioTrackNodeState {
    ChannelStripState channelStrip;
    PluginSlotState   pluginSlot;
    LatencyState      latency;
    MacroSendState    sends[MAX_TRACK_SENDS];
    uint32_t          monitorState{0}; // 0 = OFF, 1 = ON, 2 = AUTO
    uint8_t           inputHardwareChannel{0};
    std::atomic<uint32_t> totalReportedLatency{0};

    void reset(float sampleRate) {
        channelStrip.reset(sampleRate);
        pluginSlot.reset();
        latency.reset();
        for (uint32_t i = 0; i < MAX_TRACK_SENDS; ++i) {
            sends[i].reset(sampleRate);
        }
        monitorState = 0;
        inputHardwareChannel = 0;
        totalReportedLatency.store(0, std::memory_order_relaxed);
    }
};

class AudioTrackFactory : public BaseNodeFactory<AudioTrackNodeState, 1024, NODE_TYPE_AUDIO_TRACK> {
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

    void setLatency(NodeID id, uint32_t samples) {
        if (auto* state = getRegistry().get(id)) {
            state->latency.delaySamples = samples;
            state->latency.silentSamplesProcessed = 0;
            state->latency.writePos = 0;
            
            // Pre-allocate buffer rounded to the next power of 2
            uint32_t targetSize = samples + 2048; // Extra headroom
            targetSize--;
            targetSize |= targetSize >> 1;
            targetSize |= targetSize >> 2;
            targetSize |= targetSize >> 4;
            targetSize |= targetSize >> 8;
            targetSize |= targetSize >> 16;
            targetSize++;
            state->latency.bufferSize = targetSize;
            
            for (uint32_t ch = 0; ch < MAX_CHANNELS; ++ch) {
                if (state->latency.delayBuffers[ch]) delete[] state->latency.delayBuffers[ch];
                state->latency.delayBuffers[ch] = new float[state->latency.bufferSize]();
            }
        }
    }

    // PDC Latency Propagation Implementation
    uint32_t getLatency(NodeID id) const override {
        if (auto* state = getRegistry().get(id)) {
            uint32_t delay = state->latency.delaySamples;
            uint32_t reported = state->totalReportedLatency.load(std::memory_order_relaxed);
            return delay + reported;
        }
        return 0;
    }
};

void processAudioTrack(
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
