#pragma once

#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"

namespace DSP {

/**
 * @brief Analysis DSP Node State (POD)
 * 
 * High-performance metering using SIMD-accelerated Peak/RMS calculations.
 */
struct AnalysisState {
    float peak[MAX_CHANNELS] = {0.0f};
    float rms[MAX_CHANNELS] = {0.0f};
    
    // Result intended for Layer 7 UI/Meters
    float lastPeakResult[MAX_CHANNELS] = {0.0f};
    float lastRMSResult[MAX_CHANNELS] = {0.0f};

    // Circular buffer for real-time FFT spectrum visualization (channel 0)
    static constexpr uint32_t SPECTRUM_BUFFER_SIZE = 2048;
    float spectrumBuffer[SPECTRUM_BUFFER_SIZE] = {0.0f};
    uint32_t spectrumWriteIndex = 0;

    void reset() {
        for (uint32_t i = 0; i < MAX_CHANNELS; ++i) {
            peak[i] = 0.0f;
            rms[i] = 0.0f;
            lastPeakResult[i] = 0.0f;
            lastRMSResult[i] = 0.0f;
        }
        std::fill_n(spectrumBuffer, SPECTRUM_BUFFER_SIZE, 0.0f);
        spectrumWriteIndex = 0;
    }
};

/**
 * @brief Factory for creating Analysis nodes.
 */
class AnalysisFactory : public BaseNodeFactory<AnalysisState, 512, NODE_TYPE_ANALYSIS> {
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

void processAnalysis(
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
