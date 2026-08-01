#pragma once

#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"
#include <vector>

namespace DSP {

/**
 * @brief Latency DSP Node State (POD-ish)
 * 
 * Deliberately delays the signal and reports latency to the scheduler.
 */
struct LatencyState {
    uint32_t delaySamples = 0;
    
    // Internal buffer for delay (must be handled carefully in RT)
    // For simplicity in this test node, we use a fixed size vector 
    // initialized during setup.
    float* delayBuffers[MAX_CHANNELS] = {nullptr};
    uint32_t writePos = 0;
    uint32_t bufferSize = 0;
    uint32_t silentSamplesProcessed = 0; // Tracks consecutive silent samples

    void reset() {
        delaySamples = 0;
        writePos = 0;
        bufferSize = 0;
        silentSamplesProcessed = 0;
    }
};

/**
 * @brief Factory for creating Latency nodes.
 */
class LatencyFactory : public BaseNodeFactory<LatencyState, 64, NODE_TYPE_LATENCY> {
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

    void setLatency(NodeID id, uint32_t samples) {
        if (auto* s = getRegistry().get(id)) {
            s->delaySamples = samples;
            s->silentSamplesProcessed = 0;
            s->writePos = 0;
            
            // Pre-allocate buffer rounded to the next power of 2
            uint32_t targetSize = samples + 2048; // Extra headroom
            targetSize--;
            targetSize |= targetSize >> 1;
            targetSize |= targetSize >> 2;
            targetSize |= targetSize >> 4;
            targetSize |= targetSize >> 8;
            targetSize |= targetSize >> 16;
            targetSize++;
            s->bufferSize = targetSize;
            
            for (uint32_t ch = 0; ch < MAX_CHANNELS; ++ch) {
                if (s->delayBuffers[ch]) delete[] s->delayBuffers[ch];
                s->delayBuffers[ch] = new float[s->bufferSize]();
            }
        }
    }
    
    // Re-implementation of getLatency for the scheduler to find
    uint32_t getLatency(NodeID id) const override {
        if (auto* s = getRegistry().get(id)) return s->delaySamples;
        return 0;
    }
};

/**
 * @brief Standardized processing function for the Latency node.
 */
void processLatency(
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
 * @brief Core processing function for Latency state.
 * Use this when composing LatencyState inside macro-nodes.
 */
void processLatencyState(
    LatencyState* s,
    float* const* inputs,
    float* const* outputs,
    uint32_t numChannels,
    uint32_t numSamples,
    const bool* inputSilence,
    bool* isOutputSilent
);

} // namespace DSP
