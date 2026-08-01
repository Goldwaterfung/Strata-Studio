#include "audio_input_node.h"
#include <cstring>
#include <algorithm>

namespace DSP {

static auto& s_registry = AudioInputFactory::getRegistry();

void processAudioInput(
    NodeID nodeId,
    float* const* /*inputs*/,
    float* const* outputs,
    uint32_t numChannels,
    uint32_t numSamples,
    const EventData* events,
    uint32_t numEvents,
    EventData* /*outEvents*/,
    uint32_t* /*outEventCount*/,
    const ProcessContext* context,
    const bool* /*inputSilence*/,
    bool* isOutputSilent
) {
    auto* s = VALIDATE_STATE(s_registry, nodeId);
    if (!s || numSamples == 0 || !outputs) return;

    // Use double-buffering based on active topology buffer index
    uint32_t activeBufIdx = context ? (context->cycleId % 2) : 0;
    auto& activeState = s->buffers[activeBufIdx];

    // 1. Process local mutations / automation
    for (uint32_t i = 0; i < numEvents; ++i) {
        const auto& e = events[i];
        if (e.eventType == EventType::AUTOMATION) {
            if (e.payload.automation.parameterIndex == 0) {
                activeState.hardwareChannelIndex = static_cast<uint8_t>(std::clamp(e.payload.automation.targetValue, 0.0f, 255.0f));
            } else if (e.payload.automation.parameterIndex == 1) {
                activeState.numChannels = static_cast<uint8_t>(std::clamp(e.payload.automation.targetValue, 1.0f, 2.0f));
            }
        }
    }

    // 2. Evaluate logic gate (RT-safe, O(1) checks)
    bool isMonitoringActive = activeState.isMonitoringActive;

    if (!activeState.isRecordArmed && !isMonitoringActive) {
        // GATE CLOSED: Skip copying buffers. Write silence downstream.
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            if (outputs[ch]) {
                std::memset(outputs[ch], 0, numSamples * sizeof(float));
            }
        }
        if (isOutputSilent) {
            *isOutputSilent = true;
        }
        return;
    }

    uint32_t hwStart = activeState.hardwareChannelIndex;
    uint32_t hwChannels = activeState.numChannels;
    uint32_t systemInputs = (context && context->inputChannels) ? context->numInputChannels : 0;

    if (context && context->transportState == TransportState::RECORDING && activeState.isRecordArmed && activeState.recordingQueue != nullptr) {
        if (activeState.actualStartSample) {
            uint64_t expected = std::numeric_limits<uint64_t>::max();
            activeState.actualStartSample->compare_exchange_strong(expected, context->transport.positionSample);
        }
        
        bool queueFull = false;
        for (uint32_t sampleIdx = 0; sampleIdx < numSamples; ++sampleIdx) {
            if (queueFull) break;
            for (uint32_t ch = 0; ch < hwChannels; ++ch) {
                uint32_t mappedCh = hwStart + ch;
                float val = 0.0f;
                if (mappedCh < systemInputs && context->inputChannels[mappedCh]) {
                    val = context->inputChannels[mappedCh][sampleIdx];
                }
                if (!activeState.recordingQueue->push(val)) {
                    queueFull = true;
                    break;
                }
            }
        }
    }

    // 3. GATE OPENED: Copy physical input buffers or output silence for Auto-Mute
    if (isMonitoringActive && systemInputs > 0) {
        if (isOutputSilent) {
            *isOutputSilent = false;
        }
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            if (!outputs[ch]) continue;
            uint32_t mappedCh = hwStart + (ch % hwChannels);
            if (mappedCh < systemInputs && context->inputChannels[mappedCh]) {
                std::memcpy(outputs[ch], context->inputChannels[mappedCh], numSamples * sizeof(float));
            } else {
                std::memset(outputs[ch], 0, numSamples * sizeof(float));
            }
        }
    } else {
        if (isOutputSilent) {
            *isOutputSilent = true;
        }
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            if (outputs[ch]) {
                std::memset(outputs[ch], 0, numSamples * sizeof(float));
            }
        }
    }
}

} // namespace DSP
