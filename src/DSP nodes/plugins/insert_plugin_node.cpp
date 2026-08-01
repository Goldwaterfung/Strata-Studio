#include "insert_plugin_node.h"
#include "Core audio engine/plugin/iplugin.h"
#include "Core audio engine/sidechain/isidechain_manager.h"
#include "common/dsp/event_scanner.h"
#include "common/math/vector.h"

namespace DSP {

static auto& s_registry = InsertPluginFactory::getRegistry();

void processInsertPlugin(
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
    if (!s || !inputs || !outputs || numSamples == 0) return;

    EventData localEvents[256];
    uint32_t localEventCount = 0;
    uint32_t forwardedEventCount = 0;

    for (uint32_t i = 0; i < numEvents; ++i) {
        const auto& e = events[i];
        if (e.eventType == EventType::AUTOMATION && 
            e.payload.automation.targetSubNodeId != 0 && 
            e.payload.automation.targetSubNodeId != nodeId.id) {
            // Forward event meant for downstream
            if (outEvents && forwardedEventCount < 256) {
                outEvents[forwardedEventCount++] = e;
            }
        } else {
            // Event meant for this plugin
            if (localEventCount < 256) {
                localEvents[localEventCount++] = e;
            }
        }
    }
    
    if (outEventCount) {
        *outEventCount = forwardedEventCount;
    }

    // 1. Handle Bypass logic (Sample-Accurate)
    EventScanner scanner(localEvents, localEventCount);
    for (uint32_t i = 0; i < numSamples; ++i) {
        scanner.processEventsAtOffset(i, [&](const EventData& e) {
            if (e.eventType == EventType::AUTOMATION) {
                if (e.payload.automation.targetSubNodeId != 0 && e.payload.automation.targetSubNodeId != nodeId.id) {
                    return;
                }
                if (e.payload.automation.parameterIndex == BYPASS_PARAMETER_INDEX) {
                    s->bypass = (e.payload.automation.targetValue > 0.5f);
                    s->bypassRamp.setTarget(s->bypass ? 0.0f : 1.0f);
                }
            }
        });
    }

    // 2. Delegate to Plugin Instance
    bool isFullyBypassed = s->bypass && s->bypassRamp.isStatic();
    
    bool isInputSilent = (inputSilence && inputSilence[0]);
    if (!isInputSilent) {
        s->tailSamplesRemaining = (context && context->sampleRate > 0) ? static_cast<uint32_t>(context->sampleRate * 2.0f) : 88200; // 2 seconds tail
    }
    
    bool canSkipForSilence = isInputSilent && (s->tailSamplesRemaining == 0) && (localEventCount == 0);

    if (!isFullyBypassed && s->pluginInstance && !canSkipForSilence) {
        auto* plugin = static_cast<Layer3::IPlugin*>(s->pluginInstance);
        
        uint32_t pluginOutCount = 0;

        // Map main inputs and sidechain auxiliary inputs into combined planar input array
        float* combinedInputs[MAX_CHANNELS];
        bool combinedSilence[MAX_CHANNELS];
        uint32_t totalInputs = numChannels;

        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            combinedInputs[ch] = inputs[ch];
            combinedSilence[ch] = (inputSilence && inputSilence[ch]);
        }

        if (context && context->sidechainManager) {
            auto* scMgr = static_cast<Layer3::ISidechainManager*>(context->sidechainManager);
            PlanarSidechainBuffer scBuf = scMgr->getSidechainPlanarBuffer(nodeId, 1);
            if (scBuf.numChannels > 0 && scBuf.channels[0] != nullptr) {
                for (uint32_t ch = 0; ch < scBuf.numChannels && (totalInputs < MAX_CHANNELS); ++ch) {
                    combinedInputs[totalInputs] = scBuf.channels[ch];
                    combinedSilence[totalInputs] = false;
                    totalInputs++;
                }
            }
        }

        plugin->processAudio(
            combinedInputs, totalInputs,
            outputs, numChannels,
            numSamples,
            localEvents, localEventCount,
            outEvents ? outEvents + forwardedEventCount : nullptr,
            &pluginOutCount,
            context,
            combinedSilence
        );

        if (outEventCount) {
            *outEventCount += pluginOutCount;
        }

        // Apply crossfade if we are in transition
        if (!s->bypassRamp.isStatic() || s->bypass) {
            for (uint32_t i = 0; i < numSamples; ++i) {
                float wetWeight = s->bypassRamp.next();
                float dryWeight = 1.0f - wetWeight;
                for (uint32_t ch = 0; ch < numChannels; ++ch) {
                    outputs[ch][i] = (outputs[ch][i] * wetWeight) + (inputs[ch][i] * dryWeight);
                }
            }
        }

        if (isInputSilent && s->tailSamplesRemaining > 0) {
            s->tailSamplesRemaining = (s->tailSamplesRemaining > numSamples) ? s->tailSamplesRemaining - numSamples : 0;
        }
    } else if (canSkipForSilence || isFullyBypassed) {
        // Plugin is sleeping or fully bypassed, pass-through (with ramp if needed)
        for (uint32_t i = 0; i < numSamples; ++i) {
            float wetWeight = s->bypassRamp.next(); // Should be 0.0 if fully bypassed
            float dryWeight = 1.0f - wetWeight;
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                outputs[ch][i] = (inputs[ch][i] * dryWeight); // Wet is 0 here
            }
        }
        if (canSkipForSilence && isOutputSilent) {
            *isOutputSilent = true;
        }
    }

    // 3. Final Sanitization (Failsafe for 3rd party plugins)
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        Math::Vector::sanitize(outputs[ch], numSamples);
    }
}

} // namespace DSP
