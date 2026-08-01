#include "plugin_slot_node.h"
#include "insert_plugin_node.h"
#include "common/math/vector.h"
#include <cstring>

namespace DSP {

static auto& s_registry = PluginSlotFactory::getRegistry();

void processPluginSlot(
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
    auto* s = VALIDATE_STATE(s_registry, nodeId);
    if (!s) return;
    processPluginSlotState(s, inputs, outputs, numChannels, numSamples, events, numEvents, outEvents, outEventCount, context, inputSilence, isOutputSilent);
}

void processPluginSlotState(
    PluginSlotState* s,
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
    if (!s || !inputs || !outputs || numSamples == 0) return;

    bool isSilent = (inputSilence && inputSilence[0]);
    bool anyProcessed = false;
    float* const* currentInput = inputs;

    // Establish input state for the event chain
    const EventData* currentChainEvents = events;
    uint32_t currentChainCount = numEvents;

    // Pointer-swapping hooks targeting pre-allocated buffers
    EventData* currentTargetBuffer = s->scratchBufferA;
    EventData* nextChainBuffer    = s->scratchBufferB;

    uint32_t accumulatedOutCount = 0;
    constexpr uint32_t MAX_EVENTS_LIMIT = 512;

    for (uint32_t i = 0; i < PluginSlotState::MAX_SLOTS; ++i) {
        NodeID slotNodeId = s->slots[i];
        if (slotNodeId.isValid() && !s->bypass[i]) {
            uint32_t generatedCount = 0;
            bool isSlotSilent = isSilent;

            // Process the insert plugin using the current chain input
            processInsertPlugin(
                slotNodeId,
                currentInput,
                outputs,
                numChannels,
                numSamples,
                currentChainEvents,
                currentChainCount,
                currentTargetBuffer,
                &generatedCount,
                context,
                inputSilence,
                &isSlotSilent
            );

            isSilent = isSlotSilent;

            // Copy any events that need to escape to the master DAW pipeline
            if (generatedCount > 0 && outEvents && outEventCount) {
                for (uint32_t k = 0; k < generatedCount && accumulatedOutCount < MAX_EVENTS_LIMIT; ++k) {
                    outEvents[accumulatedOutCount++] = currentTargetBuffer[k];
                }
            }

            // Route audio downstream
            currentInput = outputs;
            anyProcessed = true;

            // Setup next stage event input (pointing to output of current stage)
            currentChainEvents = currentTargetBuffer;
            currentChainCount  = generatedCount;

            // Ping-pong: Swap target buffers for the next plugin in slot
            std::swap(currentTargetBuffer, nextChainBuffer);
        }
    }

    if (outEventCount) {
        *outEventCount = accumulatedOutCount;
    }

    // If no active plugins were processed, pass through input to output directly
    if (!anyProcessed) {
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            if (inputs[ch] != outputs[ch]) {
                std::memcpy(outputs[ch], inputs[ch], numSamples * sizeof(float));
            }
        }
        if (outEventCount) {
            *outEventCount = 0;
        }
    }

    if (isOutputSilent) {
        *isOutputSilent = anyProcessed ? isSilent : (inputSilence && inputSilence[0]);
    }
}

} // namespace DSP
