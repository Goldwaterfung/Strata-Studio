#include "time_pitch_node.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wdouble-promotion"
#include <rubberband/RubberBandStretcher.h>
#pragma clang diagnostic pop

#include "common/dsp/event_scanner.h"
#include "common/math/vector.h"
#include <cmath>

namespace DSP {

using namespace RubberBand;

static auto& s_registry = TimePitchFactory::getRegistry();

NodeID TimePitchFactory::createNode() {
    auto id = BaseNodeFactory::createNode();
    if (id.isValid()) {
        if (auto* s = s_registry.get(id)) {
            s->reset();
            
            // Default options for high-quality real-time warping
            int options = RubberBandStretcher::OptionProcessRealTime | 
                          RubberBandStretcher::OptionEngineFiner |
                          RubberBandStretcher::OptionTransientsCrisp;
            
            // Initializing with stereo and 44.1k (will be updated in first process call)
            s->stretcher = new RubberBandStretcher(44100, 2, options);
            s->isActive = true;
        }
    }
    return id;
}

void TimePitchFactory::destroyNode(NodeID nodeId) {
    if (auto* s = s_registry.get(nodeId)) {
        if (s->stretcher) {
            delete s->stretcher;
            s->stretcher = nullptr;
        }
    }
    BaseNodeFactory::destroyNode(nodeId);
}

uint32_t TimePitchFactory::getLatency(NodeID nodeId) const {
    if (auto* s = s_registry.get(nodeId)) {
        return s->latency;
    }
    return 0;
}

void processTimePitch(
    NodeID nodeId,
    float* const* inputs,
    float* const* outputs,
    uint32_t numChannels,
    uint32_t numSamples,
    const EventData* events,
    uint32_t numEvents,
    EventData* /*outEvents*/,
    uint32_t* /*outEventCount*/,
    const ProcessContext* /*context*/
) {
    auto* s = VALIDATE_STATE(s_registry, nodeId);
    if (!s || !s->stretcher || !inputs || !outputs || numSamples == 0) return;

    // 2. Processing with Sample-Accuracy
    EventScanner scanner(events, numEvents);
    uint32_t currentSample = 0;
    
    while (currentSample < numSamples) {
        // Handle events at this offset
        uint32_t nextEventSample = numSamples;
        bool hasAutomationEvent = false;
        
        scanner.processEventsAtOffset(currentSample, [&](const EventData& e) {
            if (e.eventType == EventType::AUTOMATION) {
                uint32_t paramIdx = e.payload.automation.parameterIndex;
                float value = e.payload.automation.targetValue;

                switch (paramIdx) {
                    case 0: // Time Ratio
                        s->timeRatio = std::max(0.1f, std::min(value, 10.0f));
                        s->ratioSmoother.setTarget(s->timeRatio, e.payload.automation.rampDuration);
                        break;
                    case 1: // Pitch Shift
                        s->pitchShift = std::max(-24.0f, std::min(value, 24.0f));
                        s->stretcher->setPitchScale(std::pow(2.0, static_cast<double>(s->pitchShift) / 12.0));
                        break;
                }
                hasAutomationEvent = true;
            }
        });

        // Find next event offset to determine sub-block size
        for (uint32_t eIdx = 0; eIdx < numEvents; ++eIdx) {
            if (events[eIdx].sampleOffset > currentSample) {
                nextEventSample = events[eIdx].sampleOffset;
                break;
            }
        }
        
        uint32_t subBlockSize = nextEventSample - currentSample;
        
        // Apply smoothing for the sub-block
        // Since we are processing sub-blocks, we should advance the smoother by the sub-block size?
        // Actually, ParameterSmoother::next() is per-sample.
        // For a sub-block, we can just use the value at the end of the sub-block?
        // Or we can loop and process per-sample?
        // Rubber Band setTimeRatio is for the block.
        
        float smoothedRatio = s->ratioSmoother.getCurrent();
        for (uint32_t i = 0; i < subBlockSize; ++i) {
            smoothedRatio = s->ratioSmoother.next();
        }
        s->stretcher->setTimeRatio(static_cast<double>(smoothedRatio));

        // Prepare sub-block input pointers
        float* subBlockInputs[MAX_CHANNELS];
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            subBlockInputs[ch] = inputs[ch] + currentSample;
        }

        // Process this sub-block
        s->stretcher->process(subBlockInputs, subBlockSize, false);
        currentSample = nextEventSample;
    }

    // 3. Retrieve Output
    // Note: In a professional implementation, we would pull into a ring buffer here
    // to smooth out Rubber Band's variable grain sizes (Glitch Protection).
    int available = s->stretcher->available();
    if (available >= static_cast<int>(numSamples)) {
        s->stretcher->retrieve(outputs, numSamples);
    } else {
        // Fallback: output silence if not enough samples available yet
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            Math::Vector::zero(outputs[ch], numSamples);
        }
    }

    // 4. Update Latency Metadata
    s->latency = static_cast<uint32_t>(s->stretcher->getLatency());

    // 5. Safety Sanitization
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        Math::Vector::sanitize(outputs[ch], numSamples);
    }
}

} // namespace DSP
