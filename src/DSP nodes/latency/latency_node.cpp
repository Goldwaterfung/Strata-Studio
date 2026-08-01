#include "latency_node.h"
#include <cstring>
#include <algorithm>

namespace DSP {

static auto& s_registry = LatencyFactory::getRegistry();

void processLatency(
    NodeID nodeId,
    float* const* inputs,
    float* const* outputs,
    uint32_t numChannels,
    uint32_t numSamples,
    const EventData* /*events*/,
    uint32_t /*numEvents*/,
    EventData* /*outEvents*/,
    uint32_t* /*outEventCount*/,
    const ProcessContext* /*context*/,
    const bool* inputSilence,
    bool* isOutputSilent
) {
    auto* s = VALIDATE_STATE(s_registry, nodeId);
    if (!s) return;
    processLatencyState(s, inputs, outputs, numChannels, numSamples, inputSilence, isOutputSilent);
}

void processLatencyState(
    LatencyState* s,
    float* const* inputs,
    float* const* outputs,
    uint32_t numChannels,
    uint32_t numSamples,
    const bool* inputSilence,
    bool* isOutputSilent
) {
    if (!s || !outputs || numSamples == 0) return;

    // 1. Short-Circuit: Zero delay or missing inputs (Pass-through copy)
    if (s->delaySamples == 0 || !inputs) {
        if (inputs && inputs != outputs) {
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                if (outputs[ch] && inputs[ch]) {
                    std::memcpy(outputs[ch], inputs[ch], numSamples * sizeof(float));
                }
            }
        }
        if (isOutputSilent) {
            *isOutputSilent = (inputs && inputSilence) ? inputSilence[0] : false;
        }
        return;
    }

    uint32_t delay = s->delaySamples;
    uint32_t size = s->bufferSize;
    uint32_t mask = size - 1; // Bitwise mask for fast index wrapping
    bool isInputSilent = (inputs && inputs[0] && inputSilence) ? inputSilence[0] : false;

    // 2. Short-Circuit: Silent & Fully Flushed (Sleep Gate)
    if (isInputSilent) {
        uint32_t prevSilent = s->silentSamplesProcessed;
        s->silentSamplesProcessed += numSamples;
        if (s->silentSamplesProcessed >= delay) {
            // Buffer is fully zeroed: Write block-level silence and update pointers in O(1)
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                if (outputs[ch]) {
                    std::memset(outputs[ch], 0, numSamples * sizeof(float));
                }
            }
            
            // Flush delay buffers on the transition edge to avoid ghost audio bursts
            if (prevSilent < delay) {
                for (uint32_t ch = 0; ch < numChannels; ++ch) {
                    if (s->delayBuffers[ch]) {
                        std::memset(s->delayBuffers[ch], 0, size * sizeof(float));
                    }
                }
            }

            s->writePos = (s->writePos + numSamples) & mask;
            if (isOutputSilent) {
                *isOutputSilent = true;
            }
            return;
        }
    } else {
        s->silentSamplesProcessed = 0; // Reset on active signal
    }

    if (isOutputSilent) {
        *isOutputSilent = false;
    }

    // 3. Active processing using fast bitwise AND mapping
    uint32_t currentWritePos = s->writePos;
    for (uint32_t i = 0; i < numSamples; ++i) {
        uint32_t readPos = (currentWritePos + size - delay) & mask;

        for (uint32_t ch = 0; ch < std::min(numChannels, MAX_CHANNELS); ++ch) {
            if (!s->delayBuffers[ch]) {
                if (inputs[ch] && outputs[ch]) outputs[ch][i] = inputs[ch][i];
                continue;
            }

            float inputSample = inputs[ch] ? inputs[ch][i] : 0.0f;
            if (outputs[ch]) outputs[ch][i] = s->delayBuffers[ch][readPos];
            s->delayBuffers[ch][currentWritePos] = inputSample;
        }
        currentWritePos = (currentWritePos + 1) & mask;
    }
    s->writePos = currentWritePos;
}

} // namespace DSP
