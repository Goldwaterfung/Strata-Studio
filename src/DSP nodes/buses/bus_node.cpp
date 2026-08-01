#include "bus_node.h"
#include "common/dsp/event_scanner.h"
#include "common/dsp/automation_fsm.h"
#include "common/math/vector.h"

namespace DSP {

static auto& s_registry = BusFactory::getRegistry();
static ITouchStateMonitor* s_automationMonitor = nullptr;

void setBusAutomationMonitor(ITouchStateMonitor* monitor) {
    s_automationMonitor = monitor;
}

void processBus(
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
        s->gainSmoother.setSampleRate(context->sampleRate);
    }

    // 1. Handle Automation
    EventScanner scanner(events, numEvents);
    scanner.processEventsAtOffset(0, [&](const EventData& e) {
        if (e.eventType == EventType::AUTOMATION) {
            uint32_t paramIdx = e.payload.automation.parameterIndex;
            float value = e.payload.automation.targetValue;

            bool canRead = s_automationMonitor ? s_automationMonitor->shouldRead(nodeId, paramIdx) : true;
            if (canRead && paramIdx == 0) {
                s->targetGain.store(value, std::memory_order_release);
            }
        }
    });

    // 2. Process Buffer
    // A bus node typically just applies a master gain to the summed input
    for (uint32_t i = 0; i < numSamples; ++i) {
        float currentGain = s->gainSmoother.process(s->targetGain);
        
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            outputs[ch][i] = inputs[ch][i] * currentGain;
        }
    }

    // 3. Safety Sanitization
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        Math::Vector::sanitize(outputs[ch], numSamples);
    }
}

} // namespace DSP
