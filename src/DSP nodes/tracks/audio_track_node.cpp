#include "DSP nodes/tracks/audio_track_node.h"
#include "common/dsp/event_scanner.h"
#include "common/math/panning.h"
#include "common/math/gain.h"
#include "common/math/vector.h"
#include <algorithm>
#include <cstring>

namespace DSP {

static auto& s_registry = AudioTrackFactory::getRegistry();

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
) {
    (void)inputs;
    (void)inputSilence;

    // 1. Validate State
    auto* s = VALIDATE_STATE(s_registry, nodeId);
    if (!s || !outputs || numSamples == 0) {
        if (outputs) {
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                if (outputs[ch]) {
                    std::memset(outputs[ch], 0, numSamples * sizeof(float));
                }
            }
        }
        if (isOutputSilent) *isOutputSilent = true;
        return;
    }

    // 2. Dynamic Sample Rate Adaptation
    float sampleRate = (context && context->sampleRate > 0.0f) ? context->sampleRate : 44100.0f;
    s->channelStrip.gainSmoother.setSampleRate(sampleRate);
    s->channelStrip.panSmoother.setSampleRate(sampleRate);
    for (uint32_t i = 0; i < MAX_TRACK_SENDS; ++i) {
        s->sends[i].gainSmoother.setSampleRate(sampleRate);
        s->sends[i].panSmoother.setSampleRate(sampleRate);
    }

    // 3. Buffer Port Hygiene: Zero all output ports (including send ports 2..9)
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        if (outputs[ch]) {
            std::memset(outputs[ch], 0, numSamples * sizeof(float));
        }
    }

    // 4. Handle Automation Events
    EventScanner scanner(events, numEvents);
    for (uint32_t i = 0; i < numSamples; ++i) {
        scanner.processEventsAtOffset(i, [&](const EventData& e) {
            if (e.eventType == EventType::AUTOMATION) {
                uint32_t paramIdx = e.payload.automation.parameterIndex;
                float value = e.payload.automation.targetValue;
                uint32_t ramp = e.payload.automation.rampDuration;

                if (paramIdx == static_cast<uint32_t>(TrackMacroParameter::Volume)) {
                    float gainLinear = Math::Gain::normalizedToLinear(value);
                    s->channelStrip.gainSmoother.setTarget(gainLinear, ramp);
                    s->channelStrip.targetGain.store(gainLinear, std::memory_order_release);
                } else if (paramIdx == static_cast<uint32_t>(TrackMacroParameter::Pan)) {
                    float clampedPan = std::clamp(value, 0.0f, 1.0f);
                    s->channelStrip.panSmoother.setTarget(clampedPan, ramp);
                    s->channelStrip.targetPan.store(clampedPan, std::memory_order_release);
                } else if (paramIdx == static_cast<uint32_t>(TrackMacroParameter::Mute)) {
                    bool muteVal = (value > 0.5f);
                    s->channelStrip.mute.store(muteVal, std::memory_order_release);
                    s->channelStrip.muteRamp.setTarget(muteVal ? 0.0f : 1.0f);
                } else if (paramIdx == static_cast<uint32_t>(TrackMacroParameter::Solo)) {
                    bool soloVal = (value > 0.5f);
                    s->channelStrip.solo.store(soloVal, std::memory_order_release);
                    s->channelStrip.soloRamp.setTarget(soloVal ? 1.0f : 1.0f);
                } else if (paramIdx == static_cast<uint32_t>(TrackMacroParameter::MonitorState)) {
                    s->monitorState = static_cast<uint32_t>(value);
                } else if (paramIdx == static_cast<uint32_t>(TrackMacroParameter::InputSourceIndex)) {
                    s->inputHardwareChannel = static_cast<uint8_t>(value);
                } else if (paramIdx >= static_cast<uint32_t>(TrackMacroParameter::Send0Gain) &&
                           paramIdx <= static_cast<uint32_t>(TrackMacroParameter::Send3PrePost)) {
                    uint32_t relIdx = paramIdx - static_cast<uint32_t>(TrackMacroParameter::Send0Gain);
                    uint32_t sendSlot = relIdx / 3;
                    uint32_t sendParam = relIdx % 3;
                    if (sendSlot < MAX_TRACK_SENDS) {
                        if (sendParam == 0) { // Gain
                            float g = Math::Gain::normalizedToLinear(value);
                            s->sends[sendSlot].gainSmoother.setTarget(g, ramp);
                            s->sends[sendSlot].targetGain.store(g, std::memory_order_release);
                        } else if (sendParam == 1) { // Pan
                            float p = std::clamp(value, 0.0f, 1.0f);
                            s->sends[sendSlot].panSmoother.setTarget(p, ramp);
                            s->sends[sendSlot].targetPan.store(p, std::memory_order_release);
                        } else if (sendParam == 2) { // PrePost
                            s->sends[sendSlot].isPreFader = (value > 0.5f);
                        }
                    }
                }
            }
        });
    }

    // 5. Temporary L1 Working Buffers for Inline Processing
    alignas(16) float tempL[1024];
    alignas(16) float tempR[1024];
    float* workBuf[2] = { tempL, tempR };
    std::memset(tempL, 0, numSamples * sizeof(float));
    std::memset(tempR, 0, numSamples * sizeof(float));

    // Determine Audio Source (Hardware input vs Clip Playback)
    bool isMonitored = (s->monitorState == 1);
    uint32_t hwBase = TRACK_INPUT_HARDWARE_PORT_BASE;
    uint32_t playBase = TRACK_INPUT_PLAYBACK_PORT_BASE;

    if (inputs) {
        if (isMonitored) {
            if (inputs[hwBase])     std::memcpy(tempL, inputs[hwBase], numSamples * sizeof(float));
            if (inputs[hwBase + 1]) std::memcpy(tempR, inputs[hwBase + 1], numSamples * sizeof(float));
        } else {
            if (inputs[playBase])     std::memcpy(tempL, inputs[playBase], numSamples * sizeof(float));
            if (inputs[playBase + 1]) std::memcpy(tempR, inputs[playBase + 1], numSamples * sizeof(float));
        }
    }

    // 6. PDC Latency Alignment Delay Buffer (Audio Rule 16: push 0.0f when silent to advance ring buffer)
    alignas(16) float pdcOutL[1024];
    alignas(16) float pdcOutR[1024];
    float* pdcBuf[2] = { pdcOutL, pdcOutR };
    processLatencyState(&s->latency, workBuf, pdcBuf, 2, numSamples, nullptr, nullptr);
    std::memcpy(tempL, pdcOutL, numSamples * sizeof(float));
    std::memcpy(tempR, pdcOutR, numSamples * sizeof(float));

    // 7. In-Place Insert Plugin Chain Execution
    processPluginSlotState(&s->pluginSlot, workBuf, workBuf, 2, numSamples, events, numEvents, outEvents, outEventCount, context, nullptr, nullptr);

    // 8. Pre-Fader Sends & Channel Strip (Main Gain / Pan) & Post-Fader Sends
    float mainGain = s->channelStrip.gainSmoother.next() * s->channelStrip.muteRamp.next() * s->channelStrip.soloRamp.next();
    float mainPan = s->channelStrip.panSmoother.next();
    float leftCoeff, rightCoeff;
    Math::Panning::calculateEqualPower(mainPan, leftCoeff, rightCoeff);

    for (uint32_t i = 0; i < numSamples; ++i) {
        float sampleL = workBuf[0][i];
        float sampleR = workBuf[1][i];

        // Process Sends
        for (uint32_t snd = 0; snd < MAX_TRACK_SENDS; ++snd) {
            auto& sendState = s->sends[snd];
            float sg = sendState.gainSmoother.next();
            if (sg > 1e-6f) {
                float sp = sendState.panSmoother.next();
                float sLeft, sRight;
                Math::Panning::calculateEqualPower(sp, sLeft, sRight);

                uint32_t outPortL = TRACK_MAIN_OUTPUT_CHANNELS + (snd * 2);
                uint32_t outPortR = outPortL + 1;

                if (sendState.isPreFader) {
                    if (outPortL < numChannels && outputs[outPortL]) outputs[outPortL][i] = sampleL * sLeft * sg;
                    if (outPortR < numChannels && outputs[outPortR]) outputs[outPortR][i] = sampleR * sRight * sg;
                } else {
                    float postL = sampleL * leftCoeff * mainGain;
                    float postR = sampleR * rightCoeff * mainGain;
                    if (outPortL < numChannels && outputs[outPortL]) outputs[outPortL][i] = postL * sLeft * sg;
                    if (outPortR < numChannels && outputs[outPortR]) outputs[outPortR][i] = postR * sRight * sg;
                }
            }
        }

        // Main Channel Strip Output
        if (numChannels >= 2 && outputs[0] && outputs[1]) {
            outputs[0][i] = sampleL * leftCoeff * mainGain;
            outputs[1][i] = sampleR * rightCoeff * mainGain;
        }
    }

    // 9. Buffer Sanitization (Safety Belt)
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        if (outputs[ch]) {
            Math::Vector::sanitize(outputs[ch], numSamples);
        }
    }

    // 10. Silence & Telemetry Evaluation
    bool srcSilent = false;
    if (inputSilence) {
        if (isMonitored) {
            srcSilent = inputSilence[hwBase] && inputSilence[hwBase + 1];
        } else {
            srcSilent = inputSilence[playBase] && inputSilence[playBase + 1];
        }
    }
    bool isSilent = (mainGain < 1e-6f) || srcSilent;
    if (isOutputSilent) {
        *isOutputSilent = isSilent;
    }

    s->channelStrip.currentGain.store(s->channelStrip.gainSmoother.getCurrent(), std::memory_order_release);
    s->channelStrip.currentPan.store(s->channelStrip.panSmoother.getCurrent(), std::memory_order_release);
}

} // namespace DSP
