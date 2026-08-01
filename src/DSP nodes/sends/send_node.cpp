#include "send_node.h"
#include "common/dsp/event_scanner.h"
#include "common/dsp/automation_fsm.h"
#include "common/math/vector.h"
#include "common/math/gain.h"

namespace DSP {

static auto& s_registry = SendFactory::getRegistry();
static ITouchStateMonitor* s_automationMonitor = nullptr;

void setSendAutomationMonitor(ITouchStateMonitor* monitor) {
    s_automationMonitor = monitor;
}

/**
 * @brief Main processing function for the Send Node.
 * 
 * It takes the input signal and scales it by the send gain.
 * The Layer 3 kernel is responsible for routing the 'outputs' of this node
 * to a summing bus.
 */
void processSend(
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
    }

    // 3. Handle Automation Events (Sample-Accurate)
    EventScanner scanner(events, numEvents);
    
    // For Sends, we use a sample-level loop to apply smoothing
    for (uint32_t i = 0; i < numSamples; ++i) {
        
        scanner.processEventsAtOffset(i, [&](const EventData& e) {
            if (e.eventType == EventType::AUTOMATION) {
                uint32_t paramIdx = e.payload.automation.parameterIndex;
                float value = e.payload.automation.targetValue;

                bool canRead = s_automationMonitor ? s_automationMonitor->shouldRead(nodeId, paramIdx) : true;
                
                if (canRead && paramIdx == 0) {
                    float gainLinear = Math::Gain::normalizedToLinear(value);
                    uint32_t ramp = e.payload.automation.rampDuration;
                    if (ramp > 0) {
                        s->gainSmoother.setTarget(gainLinear, ramp);
                    } else {
                        s->gainSmoother.setTarget(gainLinear);
                    }
                    s->targetGain.store(gainLinear, std::memory_order_release);
                }
            }
        });

        // Apply smoothed gain
        float currentGain = s->gainSmoother.next();
        
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            outputs[ch][i] = inputs[ch][i] * currentGain;
        }
    }

    // 4. Final Buffer Sanitization (Safety Belt)
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        Math::Vector::sanitize(outputs[ch], numSamples);
    }
}

} // namespace DSP
