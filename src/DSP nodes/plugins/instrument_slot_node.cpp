#include "instrument_slot_node.h"
#include "DSP nodes/tracks/instrument_track_node.h"
#include "Core audio engine/plugin/iplugin.h"
#include "common/dsp/event_scanner.h"
#include "common/math/vector.h"
#include <atomic>
#include <cstdio>

namespace DSP {

static auto& s_registry = InstrumentSlotFactory::getRegistry();

static inline void trigger_fallback_note(SineSynthState* s, uint8_t pitch, uint8_t velocity, uint8_t channel, float sampleRate) noexcept {
    for (uint32_t i = 0; i < SineSynthState::MAX_VOICES; ++i) {
        if (s->voices[i].stage != EnvelopeStage::IDLE && 
            s->voices[i].pitch == pitch && 
            s->voices[i].channel == channel) {
            s->voices[i].trigger(pitch, velocity, channel, sampleRate);
            return;
        }
    }
    for (uint32_t i = 0; i < SineSynthState::MAX_VOICES; ++i) {
        if (s->voices[i].stage == EnvelopeStage::IDLE) {
            s->voices[i].trigger(pitch, velocity, channel, sampleRate);
            return;
        }
    }
    uint32_t target_voice = 0;
    float lowest_gain = 999.0f;
    for (uint32_t i = 0; i < SineSynthState::MAX_VOICES; ++i) {
        if (s->voices[i].currentGain < lowest_gain) {
            lowest_gain = s->voices[i].currentGain;
            target_voice = i;
        }
    }
    s->voices[target_voice].trigger(pitch, velocity, channel, sampleRate);
}

static inline void release_fallback_note(SineSynthState* s, uint8_t pitch, uint8_t channel, float sampleRate) noexcept {
    for (uint32_t i = 0; i < SineSynthState::MAX_VOICES; ++i) {
        if (s->voices[i].stage != EnvelopeStage::IDLE && 
            s->voices[i].pitch == pitch && 
            s->voices[i].channel == channel) {
            s->voices[i].release(sampleRate);
        }
    }
}

// Lightweight index boundary segmenting (zero allocation, zero copy)
struct EventSegment {
    const EventData* baseEvents; // Pointer to parent block events
    uint32_t startIdx;           // Index of the first event in sub-block
    uint32_t eventCount;         // Number of events in sub-block
};

// Real-time safe, stack-friendly sub-block scanner
static inline EventSegment getSubBlockEvents(
    const EventData* sourceEvents,
    uint32_t totalEvents,
    uint32_t startSample,
    uint32_t endSample
) noexcept {
    EventSegment segment = { sourceEvents, 0, 0 };
    bool foundFirst = false;

    for (uint32_t i = 0; i < totalEvents; ++i) {
        uint32_t offset = sourceEvents[i].sampleOffset;
        if (offset >= startSample && offset < endSample) {
            if (!foundFirst) {
                segment.startIdx = i;
                foundFirst = true;
            }
            segment.eventCount++;
        }
    }
    return segment;
}

void processInstrumentSlot(
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
    processInstrumentSlotState(s, inputs, outputs, numChannels, numSamples, events, numEvents, outEvents, outEventCount, context, inputSilence, isOutputSilent);
}

void processInstrumentSlotState(
    InstrumentSlotState* s,
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

    // 1. Scan and collect unique bypass automation sample offsets into a stack-allocated array
    uint32_t splitOffsets[32];
    uint32_t numSplits = 0;
    
    splitOffsets[numSplits++] = 0;
    for (uint32_t i = 0; i < numEvents; ++i) {
        if (events[i].eventType == EventType::AUTOMATION && 
            events[i].payload.automation.parameterIndex == BYPASS_PARAMETER_INDEX) {
            uint32_t offset = events[i].sampleOffset;
            if (offset > 0 && offset < numSamples) {
                // Ensure uniqueness
                bool exists = false;
                for (uint32_t j = 0; j < numSplits; ++j) {
                    if (splitOffsets[j] == offset) {
                        exists = true;
                        break;
                    }
                }
                if (!exists && numSplits < 31) {
                    splitOffsets[numSplits++] = offset;
                }
            }
        }
    }
    
    // Sort split offsets
    for (uint32_t i = 1; i < numSplits; ++i) {
        uint32_t key = splitOffsets[i];
        int j = static_cast<int>(i) - 1;
        while (j >= 0 && splitOffsets[j] > key) {
            splitOffsets[j + 1] = splitOffsets[j];
            j--;
        }
        splitOffsets[j + 1] = key;
    }
    splitOffsets[numSplits++] = numSamples;

    // 2. Loop through each sub-block (micro-buffering)
    uint32_t totalOutEvents = 0;
    uint32_t activeChannels = std::min(numChannels, 32u);

    for (uint32_t s_idx = 0; s_idx < numSplits - 1; ++s_idx) {
        uint32_t start = splitOffsets[s_idx];
        uint32_t end = splitOffsets[s_idx + 1];
        uint32_t len = end - start;
        if (len == 0) continue;

        // Apply bypass automation target at this sub-block boundary
        for (uint32_t i = 0; i < numEvents; ++i) {
            if (events[i].sampleOffset == start && 
                events[i].eventType == EventType::AUTOMATION && 
                events[i].payload.automation.parameterIndex == BYPASS_PARAMETER_INDEX) {
                s->bypass = (events[i].payload.automation.targetValue > 0.5f);
                s->bypassRamp.setTarget(s->bypass ? 0.0f : 1.0f);
            }
        }

        bool isFullyBypassed = s->bypass && s->bypassRamp.isStatic();
        void* instancePtr = s->pluginInstance;
        std::atomic_thread_fence(std::memory_order_acquire);

        // Filter and adjust event offsets for this sub-block
        EventSegment segment = getSubBlockEvents(events, numEvents, start, end);

        // Map offsets into persistent, pre-allocated scratchpad buffer (RT-safe)
        uint32_t activeEvents = 0;
        for (uint32_t i = 0; i < segment.eventCount; ++i) {
            const auto& ev = segment.baseEvents[segment.startIdx + i];
            // Discard live MIDI events if track does not accept live MIDI (not armed or monitored)
            bool isBroadcastHardware = (ev.flags & 0x80) != 0;
            if (isBroadcastHardware && !s->acceptLiveMIDI) {
                continue;
            }
            if (activeEvents < 512) { // safe pre-allocated limit inside s->rtScratchEvents
                s->rtScratchEvents[activeEvents] = ev;
                // Translate sample offset to sub-block space
                s->rtScratchEvents[activeEvents].sampleOffset -= start;
                activeEvents++;
            }
        }

        if (!isFullyBypassed && instancePtr) {
            auto* plugin = static_cast<Layer3::IPlugin*>(instancePtr);
            
            // Define static thread-local scratchpads to guarantee 64-byte alignment
            alignas(64) thread_local static float tl_scratchInput[32][8192];
            alignas(64) thread_local static float tl_scratchOutput[32][8192];

            // Safely chunk processing if len exceeds our scratchpad size (failsafe)
            uint32_t processed = 0;
            while (processed < len) {
                uint32_t chunkLen = std::min(len - processed, 8192u);
                uint32_t chunkStart = start + processed;

                // 1. Copy & Align Inputs (if inputs exist and are not null)
                float* pluginInputs[32];
                if (inputs) {
                    for (uint32_t ch = 0; ch < activeChannels; ++ch) {
                        pluginInputs[ch] = tl_scratchInput[ch];
                        if (inputs[ch]) {
                            std::memcpy(tl_scratchInput[ch], inputs[ch] + chunkStart, chunkLen * sizeof(float));
                        } else {
                            std::memset(tl_scratchInput[ch], 0, chunkLen * sizeof(float));
                        }
                    }
                }

                // 2. Prepare Aligned Outputs
                float* pluginOutputs[32];
                for (uint32_t ch = 0; ch < activeChannels; ++ch) {
                    pluginOutputs[ch] = tl_scratchOutput[ch];
                    std::memset(tl_scratchOutput[ch], 0, chunkLen * sizeof(float));
                }

                // 3. Filter chunk-specific MIDI events
                EventData chunkEvents[512];
                uint32_t chunkEventsCount = 0;
                for (uint32_t i = 0; i < activeEvents; ++i) {
                    uint32_t evOffset = s->rtScratchEvents[i].sampleOffset;
                    if (evOffset >= processed && evOffset < processed + chunkLen) {
                        if (chunkEventsCount < 512) {
                            chunkEvents[chunkEventsCount] = s->rtScratchEvents[i];
                            chunkEvents[chunkEventsCount].sampleOffset -= processed;
                            chunkEventsCount++;
                        }
                    }
                }

                uint32_t subOutCount = 0;

                plugin->processAudio(
                    inputs ? pluginInputs : nullptr, inputs ? activeChannels : 0,
                    pluginOutputs, activeChannels,
                    chunkLen,
                    chunkEvents, chunkEventsCount,
                    outEvents ? outEvents + totalOutEvents : nullptr,
                    outEvents ? &subOutCount : nullptr,
                    context,
                    inputSilence
                );

                if (outEvents) {
                    for (uint32_t i = 0; i < subOutCount; ++i) {
                        outEvents[totalOutEvents + i].sampleOffset += chunkStart;
                    }
                    totalOutEvents += subOutCount;
                }

                // 4. Copy Outputs back to original unaligned target pointer & apply crossfade
                if (!s->bypassRamp.isStatic() || s->bypass) {
                    for (uint32_t i = 0; i < chunkLen; ++i) {
                        float wetWeight = s->bypassRamp.next();
                        float dryWeight = 1.0f - wetWeight;
                        for (uint32_t ch = 0; ch < activeChannels; ++ch) {
                            float drySample = (inputs && inputs[ch]) ? inputs[ch][chunkStart + i] : 0.0f;
                            outputs[ch][chunkStart + i] = (tl_scratchOutput[ch][i] * wetWeight) + (drySample * dryWeight);
                        }
                    }
                } else {
                    for (uint32_t ch = 0; ch < activeChannels; ++ch) {
                        std::memcpy(outputs[ch] + chunkStart, tl_scratchOutput[ch], chunkLen * sizeof(float));
                    }
                }

                processed += chunkLen;
            }
        } else if (!isFullyBypassed) {
            // Fallback to internal sine synth when no external plugin is hosted
            float sample_rate = context ? static_cast<float>(context->sampleRate) : 44100.0f;
            EventScanner fbScanner(s->rtScratchEvents, activeEvents);

            for (uint32_t i = 0; i < len; ++i) {
                fbScanner.processEventsAtOffset(i, [&](const EventData& ev) {
                    if (ev.eventType == EventType::MIDI_NOTE_ON) {
                        trigger_fallback_note(&s->fallbackSynth, ev.payload.midiNote.pitch, ev.payload.midiNote.velocity, 
                                              ev.payload.midiNote.channel, sample_rate);
                    } 
                    else if (ev.eventType == EventType::MIDI_NOTE_OFF) {
                        release_fallback_note(&s->fallbackSynth, ev.payload.midiNote.pitch, ev.payload.midiNote.channel, sample_rate);
                    }
                });

                // Synthesize and sum
                float summed_sample = 0.0f;
                for (uint32_t v = 0; v < SineSynthState::MAX_VOICES; ++v) {
                    summed_sample += s->fallbackSynth.voices[v].processSample();
                }
                summed_sample *= s->fallbackSynth.masterGain;

                // Write planar output with bypass ramp/gain if needed
                float wetWeight = s->bypassRamp.next(); // normally 1.0f if not bypassing
                float dryWeight = 1.0f - wetWeight;
                for (uint32_t ch = 0; ch < activeChannels; ++ch) {
                    float drySample = (inputs && inputs[ch]) ? inputs[ch][start + i] : 0.0f;
                    outputs[ch][start + i] = (summed_sample * wetWeight) + (drySample * dryWeight);
                }
            }
        } else {
            // Fully bypassed
            for (uint32_t i = 0; i < len; ++i) {
                float wetWeight = s->bypassRamp.next(); // Should be 0.0 if fully bypassed
                float dryWeight = 1.0f - wetWeight;
                for (uint32_t ch = 0; ch < activeChannels; ++ch) {
                    float drySample = (inputs && inputs[ch]) ? inputs[ch][start + i] : 0.0f;
                    outputs[ch][start + i] = (drySample * dryWeight);
                }
            }
        }
    }

    if (outEventCount) {
        *outEventCount = totalOutEvents;
    }

    // 3. Final Sanitization (Failsafe for 3rd party plugins)
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        Math::Vector::sanitize(outputs[ch], numSamples);
    }
}

} // namespace DSP
