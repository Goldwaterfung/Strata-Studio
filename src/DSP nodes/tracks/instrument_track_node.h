#pragma once
#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"
#include "DSP nodes/channelstrip/channel_strip_node.h"
#include "DSP nodes/plugins/plugin_slot_node.h"
#include "DSP nodes/plugins/instrument_slot_node.h"
#include "DSP nodes/sends/send_node.h"
#include "DSP nodes/latency/latency_node.h"

namespace DSP {

struct InstrumentTrackNodeState {
    InstrumentSlotState instrumentSlot;
    ChannelStripState   channelStrip;
    PluginSlotState     pluginSlot;
    LatencyState        latency;
    MacroSendState      sends[MAX_TRACK_SENDS];
    uint32_t            monitorState{0};
    std::atomic<uint32_t> totalReportedLatency{0};

    void reset(float sampleRate) {
        instrumentSlot.reset();
        channelStrip.reset(sampleRate);
        pluginSlot.reset();
        latency.reset();
        for (uint32_t i = 0; i < MAX_TRACK_SENDS; ++i) {
            sends[i].reset(sampleRate);
        }
        monitorState = 0;
        totalReportedLatency.store(0, std::memory_order_relaxed);
    }
};

class InstrumentTrackFactory : public BaseNodeFactory<InstrumentTrackNodeState, 1024, NODE_TYPE_INSTRUMENT_TRACK> {
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

    uint32_t getLatency(NodeID id) const override {
        if (auto* state = getRegistry().get(id)) {
            return state->latency.delaySamples + state->totalReportedLatency.load(std::memory_order_relaxed);
        }
        return 0;
    }
};

void processInstrumentTrack(
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
 * @brief Helper function to retrieve InstrumentSlotState* from either a standalone
 * InstrumentSlotNode or an embedded instrument slot inside an InstrumentTrackNode.
 */
inline InstrumentSlotState* getInstrumentSlotState(NodeID nodeId) {
    if (!nodeId.isValid()) return nullptr;
    if (auto* slotNode = InstrumentSlotFactory::getRegistry().get(nodeId)) {
        return slotNode;
    }
    if (auto* trkNode = InstrumentTrackFactory::getRegistry().get(nodeId)) {
        return &trkNode->instrumentSlot;
    }
    return nullptr;
}

} // namespace DSP
