#include "sine_synth_node.h"
#include "common/dsp/event_scanner.h"
#include <cstdio>

namespace DSP {

static auto& s_registry = SineSynthFactory::getRegistry();

// Allocate or steal voice slot in the RT audio loop
inline void trigger_note(SineSynthState* s, uint8_t pitch, uint8_t velocity, uint8_t channel, float sampleRate) noexcept {
    // 1. Check if note is already playing on this channel (re-trigger)
    for (uint32_t i = 0; i < SineSynthState::MAX_VOICES; ++i) {
        if (s->voices[i].stage != EnvelopeStage::IDLE && 
            s->voices[i].pitch == pitch && 
            s->voices[i].channel == channel) {
            s->voices[i].trigger(pitch, velocity, channel, sampleRate);
            return;
        }
    }

    // 2. Find empty voice slot
    for (uint32_t i = 0; i < SineSynthState::MAX_VOICES; ++i) {
        if (s->voices[i].stage == EnvelopeStage::IDLE) {
            s->voices[i].trigger(pitch, velocity, channel, sampleRate);
            return;
        }
    }

    // 3. Voice Stealing: Find active voice with lowest volume (deepest in release stage)
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

inline void release_note(SineSynthState* s, uint8_t pitch, uint8_t channel, float sampleRate) noexcept {
    for (uint32_t i = 0; i < SineSynthState::MAX_VOICES; ++i) {
        if (s->voices[i].stage != EnvelopeStage::IDLE && 
            s->voices[i].pitch == pitch && 
            s->voices[i].channel == channel) {
            s->voices[i].release(sampleRate);
        }
    }
}

void processSineSynth(
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
    bool* /*isOutputSilent*/
) {
    auto* s = VALIDATE_STATE(s_registry, nodeId);
    if (!s || !outputs || numSamples == 0) return;

    float sample_rate = context ? static_cast<float>(context->sampleRate) : 44100.0f;
    EventScanner scanner(events, numEvents);

    for (uint32_t i = 0; i < numSamples; ++i) {
        // 1. Process sample-accurate events at this specific index
        scanner.processEventsAtOffset(i, [&](const EventData& ev) {
            if (ev.eventType == EventType::MIDI_NOTE_ON) {
                printf("[SineSynthNode] Node %u - MIDI_NOTE_ON: pitch=%u velocity=%u channel=%u at offset=%u\n", 
                       nodeId.id, ev.payload.midiNote.pitch, ev.payload.midiNote.velocity, ev.payload.midiNote.channel, i);
                trigger_note(s, ev.payload.midiNote.pitch, ev.payload.midiNote.velocity, 
                             ev.payload.midiNote.channel, sample_rate);
            } 
            else if (ev.eventType == EventType::MIDI_NOTE_OFF) {
                printf("[SineSynthNode] Node %u - MIDI_NOTE_OFF: pitch=%u channel=%u at offset=%u\n", 
                       nodeId.id, ev.payload.midiNote.pitch, ev.payload.midiNote.channel, i);
                release_note(s, ev.payload.midiNote.pitch, ev.payload.midiNote.channel, sample_rate);
            }
        });

        // 2. Synthesize and sum all active voices
        float summed_sample = 0.0f;
        for (uint32_t v = 0; v < SineSynthState::MAX_VOICES; ++v) {
            summed_sample += s->voices[v].processSample();
        }
        summed_sample *= s->masterGain;

        // 3. Write planar audio out to all active channels (e.g. Stereo Stereo Summing)
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            outputs[ch][i] = summed_sample;
        }
    }
}

} // namespace DSP
