#include "panner_node.h"
#include "common/dsp/event_scanner.h"
#include "common/dsp/automation_fsm.h"
#include "common/math/panning.h"
#include "common/math/vector.h"
#include <algorithm>

namespace DSP {

static auto& s_registry = PannerFactory::getRegistry();
static ITouchStateMonitor* s_automationMonitor = nullptr;

void setPannerAutomationMonitor(ITouchStateMonitor* monitor) {
    s_automationMonitor = monitor;
}

void processPanner(
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
    const bool* /*inputSilence*/,
    bool* /*isOutputSilent*/
) {
    auto* s = VALIDATE_STATE(s_registry, nodeId);
    if (!s || !inputs || !outputs || numSamples == 0) return;

    if (context && context->sampleRate > 0) {
        s->panSmoother.setSampleRate(context->sampleRate);
        s->widthSmoother.setSampleRate(context->sampleRate);
    }

    EventScanner scanner(events, numEvents);

    bool isStatic = (numEvents == 0) && s->panSmoother.isStatic() && s->widthSmoother.isStatic();

    if (isStatic) {
        float currentPan = s->panSmoother.getCurrent();
        float currentWidth = s->widthSmoother.getCurrent();
        float left, right;
        
        if (s->mode == 0) {
            Math::Panning::calculateEqualPower(currentPan, left, right);
        } else {
            Math::Panning::calculateLinear(currentPan, left, right);
        }

        for (uint32_t i = 0; i < numSamples; ++i) {
            if (numChannels >= 2) {
                float inL = inputs[0][i];
                float inR = inputs[1][i];
                float mid = (inL + inR) * 0.5f;
                float side = (inL - inR) * 0.5f;
                float wL = mid + side * currentWidth;
                float wR = mid - side * currentWidth;
                outputs[0][i] = wL * left;
                outputs[1][i] = wR * right;
                for (uint32_t ch = 2; ch < numChannels; ++ch) {
                    outputs[ch][i] = inputs[ch][i]; // Passthrough
                }
            } else {
                outputs[0][i] = inputs[0][i] * (left + right) * 0.5f;
            }
        }
    } else {
        for (uint32_t i = 0; i < numSamples; ++i) {
            scanner.processEventsAtOffset(i, [&](const EventData& e) {
                if (e.eventType == EventType::AUTOMATION) {
                    uint32_t paramIdx = e.payload.automation.parameterIndex;
                    float value = e.payload.automation.targetValue;

                    bool canRead = s_automationMonitor ? s_automationMonitor->shouldRead(nodeId, paramIdx) : true;
                    
                    if (canRead) {
                        switch (paramIdx) {
                            case 0: s->panSmoother.setTarget(std::clamp(value, 0.0f, 1.0f), e.payload.automation.rampDuration); s->targetPan.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_release); break;
                            case 1: s->widthSmoother.setTarget(std::clamp(value, 0.0f, 2.0f), e.payload.automation.rampDuration); s->targetWidth.store(std::clamp(value, 0.0f, 2.0f), std::memory_order_release); break;
                            case 2: s->mode = static_cast<uint32_t>(value); break;
                        }
                    }
                }
            });

            float currentPan = s->panSmoother.next();
            float currentWidth = s->widthSmoother.next();

            float left, right;
            if (s->mode == 0) {
                Math::Panning::calculateEqualPower(currentPan, left, right);
            } else {
                Math::Panning::calculateLinear(currentPan, left, right);
            }

            if (numChannels >= 2) {
                float inL = inputs[0][i];
                float inR = inputs[1][i];

                // Width Control (M/S technique)
                // Mid = (L+R)/2, Side = (L-R)/2
                // L = Mid + Side * width, R = Mid - Side * width
                float mid = (inL + inR) * 0.5f;
                float side = (inL - inR) * 0.5f;
                
                float wL = mid + side * currentWidth;
                float wR = mid - side * currentWidth;

                outputs[0][i] = wL * left;
                outputs[1][i] = wR * right;
                
                for (uint32_t ch = 2; ch < numChannels; ++ch) {
                    outputs[ch][i] = inputs[ch][i]; // Passthrough
                }
            } else {
                // Mono: Width is ignored, just pan (which acts as gain if we don't have a dest)
                outputs[0][i] = inputs[0][i] * (left + right) * 0.5f;
            }
        }
    }

    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        Math::Vector::sanitize(outputs[ch], numSamples);
    }
}

} // namespace DSP
