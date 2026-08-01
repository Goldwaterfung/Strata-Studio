#pragma once

#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"
#include <cmath>

namespace DSP {

// Pre-allocated envelope states
enum class EnvelopeStage : uint8_t {
    IDLE,
    ATTACK,
    RELEASE
};

struct SynthVoice {
    uint8_t pitch        = 0;       // MIDI Note number (0-127)
    uint8_t channel      = 0;       // MIDI Channel (0-15)
    float phase          = 0.0f;    // Oscillator phase [0.0, 2*PI]
    float phaseStep      = 0.0f;    // Frequency-derived phase step per sample
    float targetGain     = 0.0f;    // Target volume from note velocity
    
    // Simplistic envelope variables (Attack-Release only for WCET determinism)
    EnvelopeStage stage  = EnvelopeStage::IDLE;
    float currentGain    = 0.0f;
    float envelopeStep   = 0.0f;    // Gain change step per sample

    void trigger(uint8_t note, uint8_t vel, uint8_t chan, float sampleRate) noexcept {
        pitch      = note;
        channel    = chan;
        phase      = 0.0f;
        targetGain = static_cast<float>(vel) / 127.0f * 0.25f; // Scale to avoid clipping
        stage      = EnvelopeStage::ATTACK;
        currentGain = 0.0f;

        // Calculate phase delta for the note pitch frequency
        float freq = 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
        phaseStep  = (freq * 2.0f * 3.14159265f) / sampleRate;

        // Simple 5ms attack time constant
        envelopeStep = targetGain / (0.005f * sampleRate);
    }

    void release(float sampleRate) noexcept {
        stage = EnvelopeStage::RELEASE;
        // Simple 50ms release envelope decay time constant
        envelopeStep = currentGain / (0.050f * sampleRate);
    }

    inline float processSample() noexcept {
        if (stage == EnvelopeStage::IDLE) return 0.0f;

        // Generate oscillator value
        float sample = std::sin(phase) * currentGain;
        phase += phaseStep;
        if (phase >= 2.0f * 3.14159265f) {
            phase -= 2.0f * 3.14159265f;
        }

        // Apply envelope state machine
        if (stage == EnvelopeStage::ATTACK) {
            currentGain += envelopeStep;
            if (currentGain >= targetGain) {
                currentGain = targetGain;
                stage = EnvelopeStage::RELEASE; // Fall into release tail or sustain
            }
        } 
        else if (stage == EnvelopeStage::RELEASE) {
            currentGain -= envelopeStep;
            if (currentGain <= 0.0001f) {
                currentGain = 0.0f;
                stage = EnvelopeStage::IDLE; // Free voice slot
            }
        }

        return sample;
    }
};

struct SineSynthState {
    static constexpr uint32_t MAX_VOICES = 32; // Deterministic Voice count
    SynthVoice voices[MAX_VOICES];
    float masterGain = 0.8f;

    void reset() {
        masterGain = 0.8f;
        for (uint32_t i = 0; i < MAX_VOICES; ++i) {
            voices[i] = SynthVoice{};
        }
    }
};

class SineSynthFactory : public BaseNodeFactory<SineSynthState, 1024, NODE_TYPE_SINE_SYNTH> {
public:
    NodeID createNode() override {
        auto id = BaseNodeFactory::createNode();
        if (id.isValid()) {
            if (auto* state = getRegistry().get(id)) {
                state->reset();
            }
        }
        return id;
    }
};

void processSineSynth(
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
);

} // namespace DSP
