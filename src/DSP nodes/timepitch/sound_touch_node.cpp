#include "sound_touch_node.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wold-style-cast"
#include <SoundTouch.h>
#pragma clang diagnostic pop

#include "common/dsp/event_scanner.h"
#include "common/math/vector.h"
#include <algorithm>
#include <cmath>

namespace DSP {

using namespace soundtouch;

static auto& s_registry = SoundTouchFactory::getRegistry();

NodeID SoundTouchFactory::createNode() {
    auto id = BaseNodeFactory::createNode();
    if (id.isValid()) {
        if (auto* s = s_registry.get(id)) {
            s->reset();
            s->engine = new SoundTouch();
            
            // Default configuration
            s->engine->setSampleRate(44100);
            s->engine->setChannels(2);
            s->engine->setTempo(1.0);
            
            // Disable internal OpenMP for the DSP path to avoid thread contention
            // Centralized Layer 3 Scheduler will handle track parallelism
            s->engine->setSetting(SETTING_USE_QUICKSEEK, 1); // Default to quick for performance
            
            s->isActive = true;
        }
    }
    return id;
}

void SoundTouchFactory::destroyNode(NodeID nodeId) {
    if (auto* s = s_registry.get(nodeId)) {
        if (s->engine) {
            delete s->engine;
            s->engine = nullptr;
        }
    }
    BaseNodeFactory::destroyNode(nodeId);
}

uint32_t SoundTouchFactory::getLatency(NodeID nodeId) const {
    if (auto* s = s_registry.get(nodeId)) {
        return s->latency;
    }
    return 0;
}

/**
 * @brief Internal helper to push samples into the jitter buffer
 */
static void pushToJitterBuffer(SoundTouchState* s, const float* interleaved, uint32_t numSamples, uint32_t numChannels) {
    if (numSamples == 0) return;

    // Check for overflow - in a DAW we shouldn't drop samples, but we must protect memory
    if (s->jitterLevel + numSamples > SOUNDTOUCH_JITTER_CAPACITY) {
        // This shouldn't happen if the buffer is sized correctly for the max stretch ratio
        return; 
    }

    // Deinterleave directly into the ring buffer
    for (uint32_t i = 0; i < numSamples; ++i) {
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            s->jitterBuffer[ch][s->jitterWriteIdx] = interleaved[i * numChannels + ch];
        }
        s->jitterWriteIdx = (s->jitterWriteIdx + 1) % SOUNDTOUCH_JITTER_CAPACITY;
    }
    s->jitterLevel += numSamples;
}

/**
 * @brief Internal helper to pull samples from the jitter buffer
 */
static void pullFromJitterBuffer(SoundTouchState* s, float* const* outputs, uint32_t numSamples, uint32_t numChannels) {
    uint32_t samplesToRead = std::min(numSamples, s->jitterLevel);
    
    for (uint32_t i = 0; i < samplesToRead; ++i) {
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            outputs[ch][i] = s->jitterBuffer[ch][s->jitterReadIdx];
        }
        s->jitterReadIdx = (s->jitterReadIdx + 1) % SOUNDTOUCH_JITTER_CAPACITY;
    }
    s->jitterLevel -= samplesToRead;

    // If we didn't have enough samples, fill the rest with silence
    if (samplesToRead < numSamples) {
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            Math::Vector::zero(outputs[ch] + samplesToRead, numSamples - samplesToRead);
        }
    }
}

void processSoundTouch(
    NodeID nodeId,
    float* const* inputs,
    float* const* outputs,
    uint32_t numChannels,
    uint32_t numSamples,
    const EventData* events,
    uint32_t numEvents,
    EventData* /*outEvents*/,
    uint32_t* /*outEventCount*/,
    const ProcessContext* context
) {
    auto* s = VALIDATE_STATE(s_registry, nodeId);
    if (!s || !s->engine || !inputs || !outputs || numSamples == 0) return;

    // Update sample rate if it changed
    if (context->sampleRate != static_cast<float>(s->sampleRate)) {
        s->sampleRate = static_cast<uint32_t>(context->sampleRate);
        s->engine->setSampleRate(s->sampleRate);
        s->ratioSmoother.init(s->params.timeRatio, 20.0f, context->sampleRate);
    }
    
    if (numChannels != s->numChannels) {
        s->numChannels = static_cast<uint16_t>(numChannels);
        s->engine->setChannels(s->numChannels);
    }

    // Bypass mode
    if (s->params.warpMode == WarpMode::BYPASS) {
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            Math::Vector::copy(outputs[ch], inputs[ch], numSamples);
        }
        s->latency = 0;
        return;
    }

    // Sub-block processing for automation
    EventScanner scanner(events, numEvents);
    uint32_t currentSample = 0;

    while (currentSample < numSamples) {
        uint32_t nextEventSample = numSamples;
        
        scanner.processEventsAtOffset(currentSample, [&](const EventData& e) {
            if (e.eventType == EventType::AUTOMATION) {
                switch (e.payload.automation.parameterIndex) {
                    case 0: // Time Ratio
                        s->params.timeRatio = std::max(0.1f, std::min(e.payload.automation.targetValue, 10.0f));
                        s->ratioSmoother.setTarget(s->params.timeRatio, e.payload.automation.rampDuration);
                        break;
                    case 1: // Pitch Shift
                        s->params.pitchSemiTones = std::max(-24.0f, std::min(e.payload.automation.targetValue, 24.0f));
                        s->engine->setPitchSemiTones(static_cast<double>(s->params.pitchSemiTones));
                        break;
                }
            }
        });

        // Determine sub-block size
        for (uint32_t i = 0; i < numEvents; ++i) {
            if (events[i].sampleOffset > currentSample) {
                nextEventSample = events[i].sampleOffset;
                break;
            }
        }
        uint32_t subBlockSize = nextEventSample - currentSample;

        // Apply smoothing and update engine
        float currentRatio = s->ratioSmoother.getCurrent();
        for (uint32_t i = 0; i < subBlockSize; ++i) {
            currentRatio = s->ratioSmoother.next();
        }
        s->engine->setTempo(static_cast<double>(currentRatio));

        // Interleave input and put into engine
        // Note: SoundTouch handles interleaved float data
        Math::Vector::interleave(s->interleavedScratch, inputs, numChannels, subBlockSize);
        s->engine->putSamples(s->interleavedScratch, subBlockSize);

        // Drain engine into jitter buffer
        uint32_t received;
        do {
            received = s->engine->receiveSamples(s->interleavedScratch, 2048);
            pushToJitterBuffer(s, s->interleavedScratch, received, numChannels);
        } while (received > 0);

        currentSample = nextEventSample;
    }

    // Pull from jitter buffer to final output
    pullFromJitterBuffer(s, outputs, numSamples, numChannels);

    // Update latency reporting
    s->latency = static_cast<uint32_t>(s->engine->getSetting(SETTING_NOMINAL_INPUT_SEQUENCE));

    // Safety sanitization
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        Math::Vector::sanitize(outputs[ch], numSamples);
    }
}

} // namespace DSP
