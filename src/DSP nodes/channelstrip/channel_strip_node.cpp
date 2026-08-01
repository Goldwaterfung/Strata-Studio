#include "channel_strip_node.h"
#include "common/dsp/event_scanner.h"
#include "common/dsp/automation_fsm.h"
#include "common/math/panning.h"
#include "common/math/vector.h"
#include "common/math/gain.h"
#include <algorithm>

namespace DSP {

// Registry for this module (referenced by the factory and process func)
static auto& s_registry = ChannelStripFactory::getRegistry();

// Global (or per-kernel) automation monitor
static ITouchStateMonitor* s_automationMonitor = nullptr;

void setChannelStripAutomationMonitor(ITouchStateMonitor* monitor) {
    s_automationMonitor = monitor;
}

/**
 * @brief Main processing function for the Channel Strip.
 */
void processChannelStrip(
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
    // 1. Validate State
    auto* s = VALIDATE_STATE(s_registry, nodeId);
    if (!s || !inputs || !outputs || numSamples == 0) return;

    // 2. Update Sample Rate if changed
    if (context && context->sampleRate > 0) {
        s->gainSmoother.setSampleRate(context->sampleRate);
        s->panSmoother.setSampleRate(context->sampleRate);
    }

    // 3. Initialize Event Scanner
    EventScanner scanner(events, numEvents);

    bool isStatic = (numEvents == 0) && s->gainSmoother.isStatic() && s->panSmoother.isStatic() && s->muteRamp.isStatic() && s->soloRamp.isStatic();

    if (isStatic) {
        float currentGain = s->gainSmoother.getCurrent() * s->muteRamp.getCurrent() * s->soloRamp.getCurrent();
        float currentPan = s->panSmoother.getCurrent();
        float left, right;
        Math::Panning::calculateEqualPower(currentPan, left, right);

        for (uint32_t i = 0; i < numSamples; ++i) {
            if (numChannels >= 2) {
                outputs[0][i] = inputs[0][i] * left * currentGain;
                outputs[1][i] = inputs[1][i] * right * currentGain;
                
                for (uint32_t ch = 2; ch < numChannels; ++ch) {
                    outputs[ch][i] = inputs[ch][i] * currentGain;
                }
            } else {
                outputs[0][i] = inputs[0][i] * currentGain;
            }
        }
    } else {
        // 4. Sample-Processing Loop
        for (uint32_t i = 0; i < numSamples; ++i) {
            
            // Handle Automation Events at this sample offset
            scanner.processEventsAtOffset(i, [&](const EventData& e) {
                if (e.eventType == EventType::AUTOMATION) {
                    uint32_t paramIdx = e.payload.automation.parameterIndex;
                    float value = e.payload.automation.targetValue;

                    // Decision via FSM: Should we read this automation value?
                    bool canRead = s_automationMonitor ? s_automationMonitor->shouldRead(nodeId, paramIdx) : true;
                    
                    if (canRead) {
                        // Check if we are gliding back
                        uint32_t glideLeft = s_automationMonitor ? s_automationMonitor->getGlideSamples(nodeId, paramIdx) : 0;
                        
                        if (glideLeft > 0) {
                            // 50ms glide transition
                            if (paramIdx == static_cast<uint32_t>(ChannelStripParameter::Volume)) s->gainSmoother.updateAlpha(50.0f, context ? context->sampleRate : 44100.0f);
                            if (paramIdx == static_cast<uint32_t>(ChannelStripParameter::Pan)) s->panSmoother.updateAlpha(50.0f, context ? context->sampleRate : 44100.0f);
                            s_automationMonitor->decrementGlide(nodeId, paramIdx, 1);
                        } else {
                            // Regular 10ms smoothing
                            if (paramIdx == static_cast<uint32_t>(ChannelStripParameter::Volume)) s->gainSmoother.updateAlpha(10.0f, context ? context->sampleRate : 44100.0f);
                            if (paramIdx == static_cast<uint32_t>(ChannelStripParameter::Pan)) s->panSmoother.updateAlpha(10.0f, context ? context->sampleRate : 44100.0f);
                        }

                        switch (static_cast<ChannelStripParameter>(paramIdx)) {
                            case ChannelStripParameter::Volume: {
                                float gainLinear = Math::Gain::normalizedToLinear(value);
                                s->gainSmoother.setTarget(gainLinear, e.payload.automation.rampDuration);
                                s->targetGain.store(gainLinear, std::memory_order_release);
                                break;
                            }
                            case ChannelStripParameter::Pan: s->panSmoother.setTarget(std::clamp(value, 0.0f, 1.0f), e.payload.automation.rampDuration); s->targetPan.store(std::clamp(value, 0.0f, 1.0f), std::memory_order_release); break;
                            case ChannelStripParameter::Mute: {
                                bool muteVal = (value > 0.5f);
                                s->mute.store(muteVal, std::memory_order_release);
                                s->muteRamp.setTarget(muteVal ? 0.0f : 1.0f);
                                break;
                            }
                            case ChannelStripParameter::Solo: {
                                break; // Ignored for real-time automation
                            }
                        }
                    }
                }
            });

            // 1. Calculate base gain and pan
            float currentGain = s->gainSmoother.next();
            float currentPan = s->panSmoother.next();

            // 3. Apply Mute/Solo Ramps
            currentGain *= s->muteRamp.next();
            // Note: Solo logic typically involves muting other tracks, but on the strip level
            // we can use it to force enable if needed. For now, we follow the spec's ramp requirement.
            currentGain *= s->soloRamp.next();

            // 4. Panning Law
            float left, right;
            Math::Panning::calculateEqualPower(currentPan, left, right);

            // 5. Apply Gain & Pan
            if (numChannels >= 2) {
                outputs[0][i] = inputs[0][i] * left * currentGain;
                outputs[1][i] = inputs[1][i] * right * currentGain;
                
                for (uint32_t ch = 2; ch < numChannels; ++ch) {
                    outputs[ch][i] = inputs[ch][i] * currentGain;
                }
            } else {
                outputs[0][i] = inputs[0][i] * currentGain;
            }
        }
    }

    // 5. Final Buffer Sanitization (Safety Belt)
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        Math::Vector::sanitize(outputs[ch], numSamples);
    }
    
    // 6. Update current telemetry for UI sync
    s->currentGain.store(s->gainSmoother.getCurrent(), std::memory_order_release);
    s->currentPan.store(s->panSmoother.getCurrent(), std::memory_order_release);
}

} // namespace DSP
