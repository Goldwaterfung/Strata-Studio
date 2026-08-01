#include "monitor_switch_node.h"
#include <cstring>
#include <algorithm>

namespace DSP {

static auto& s_registry = MonitorSwitchFactory::getRegistry();

void processMonitorSwitch(
    NodeID nodeId,
    float* const* inputs,
    float* const* outputs,
    uint32_t numChannels,
    uint32_t numSamples,
    const EventData* events,
    uint32_t numEvents,
    EventData* /*outEvents*/,
    uint32_t* /*outEventCount*/,
    const ProcessContext* context,
    const bool* inputSilence,
    bool* isOutputSilent
) {
    auto* s = VALIDATE_STATE(s_registry, nodeId);
    if (!s || numSamples == 0 || !outputs) return;

    // 1. Process mutations (monitorState updates)
    for (uint32_t i = 0; i < numEvents; ++i) {
        const auto& e = events[i];
        if (e.eventType == EventType::AUTOMATION) {
            if (e.payload.automation.parameterIndex == 0) {
                s->monitorState = static_cast<uint8_t>(std::clamp(e.payload.automation.targetValue, 0.0f, 2.0f));
            }
        }
    }

    // 2. Resolve monitoring rules (O(1) checks)
    bool playInput = (s->monitorState == 1);
    bool playPlayback = (s->monitorState == 0);
    if (s->monitorState == 2) { // AUTO
        bool isRecording = (context && context->transportState == TransportState::RECORDING);
        playInput = true;
        playPlayback = !isRecording;
    }

    // 3. Retrieve input signals
    const float* inPhysicalL = (inputs && playInput) ? inputs[0] : nullptr;
    const float* inPhysicalR = (inputs && playInput && numChannels > 1) ? inputs[1] : nullptr;
    const float* inPlaybackL = (inputs && playPlayback) ? inputs[2] : nullptr;
    const float* inPlaybackR = (inputs && playPlayback && numChannels > 1) ? inputs[3] : nullptr;

    // Query silence flags of input channels (provided by scheduler)
    bool physicalSilent = !inPhysicalL || (inputSilence && inputSilence[0]);
    bool playbackSilent = !inPlaybackL || (inputSilence && inputSilence[2]);

    bool allOutputsSilent = true;

    // 4. Short-Circuit Routing & Summing
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        if (!outputs[ch]) continue;

        const float* inPhys = (ch == 0) ? inPhysicalL : inPhysicalR;
        const float* inPlay = (ch == 0) ? inPlaybackL : inPlaybackR;
        float* out = outputs[ch];

        if (playInput && playPlayback) {
            if (physicalSilent && playbackSilent) {
                // Both silent: Bypasses copy entirely. Downstream reads silence.
                std::memset(out, 0, numSamples * sizeof(float));
            } else if (physicalSilent && inPlay) {
                // Input is silent: Degrade to simple copy of playback
                std::memcpy(out, inPlay, numSamples * sizeof(float));
                allOutputsSilent = false;
            } else if (playbackSilent && inPhys) {
                // Playback is silent: Degrade to simple copy of input
                std::memcpy(out, inPhys, numSamples * sizeof(float));
                allOutputsSilent = false;
            } else if (inPhys && inPlay) {
                // Both active: Perform SIMD-optimized vector summing
                // (Modern compiler vectorizes this loop automatically under -O3 / -ffast-math)
                allOutputsSilent = false;
                #pragma omp simd
                for (uint32_t i = 0; i < numSamples; ++i) {
                    out[i] = inPhys[i] + inPlay[i];
                }
            } else {
                std::memset(out, 0, numSamples * sizeof(float));
            }
        } else if (playInput && inPhys && !physicalSilent) {
            // Input-Only routing
            std::memcpy(out, inPhys, numSamples * sizeof(float));
            allOutputsSilent = false;
        } else if (playPlayback && inPlay && !playbackSilent) {
            // Playback-Only routing
            std::memcpy(out, inPlay, numSamples * sizeof(float));
            allOutputsSilent = false;
        } else {
            // Suspended/Silent routing
            std::memset(out, 0, numSamples * sizeof(float));
        }
    }

    if (isOutputSilent) {
        *isOutputSilent = allOutputsSilent;
    }
}

} // namespace DSP
