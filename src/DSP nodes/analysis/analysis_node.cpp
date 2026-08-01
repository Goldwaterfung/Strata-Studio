#include "analysis_node.h"
#include <Eigen/Core>
#include "common/math/vector.h"
#include <cmath>
#include "Core infrastructure/bridges/itelemetry_bridge.h"

namespace DSP {

static auto& s_registry = AnalysisFactory::getRegistry();
static Layer2::ITelemetryBridge* s_telemetryBridge = nullptr;

void processAnalysis(
    NodeID nodeId,
    float* const* inputs,
    float* const* outputs,
    uint32_t numChannels,
    uint32_t numSamples,
    const EventData* /*events*/,
    uint32_t /*numEvents*/,
    EventData* outEvents,
    uint32_t* outEventCount,
    const ProcessContext* /*context*/,
    const bool* /*inputSilence*/,
    bool* /*isOutputSilent*/
) {
    auto* s = VALIDATE_STATE(s_registry, nodeId);
    if (!s || !inputs || numSamples == 0) return;

    // Analysis nodes are typically non-modifying passthrough
    if (outputs && inputs != outputs) {
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            Math::Vector::copy(outputs[ch], inputs[ch], numSamples);
        }
    }

    // SIMD-accelerated Peak/RMS calculation using Eigen
    for (uint32_t ch = 0; ch < numChannels && ch < MAX_CHANNELS; ++ch) {
        if (!inputs[ch]) continue;

        Eigen::Map<const Eigen::VectorXf> vec(inputs[ch], numSamples);
        
        // 1. Peak Level
        s->lastPeakResult[ch] = vec.array().abs().maxCoeff();

        // 2. RMS Level
        float sumSquares = vec.dot(vec);
        s->lastRMSResult[ch] = std::sqrt(sumSquares / static_cast<float>(numSamples));
        
        // 2.5 Circular Buffer for FFT Spectrum Visualization (channel 0/Left)
        if (ch == 0) {
            uint32_t writeIdx = s->spectrumWriteIndex;
            for (uint32_t i = 0; i < numSamples; ++i) {
                s->spectrumBuffer[writeIdx] = inputs[ch][i];
                writeIdx = (writeIdx + 1) % AnalysisState::SPECTRUM_BUFFER_SIZE;
            }
            s->spectrumWriteIndex = writeIdx;
        }
        
        // 3. Telemetry Dispatch
        if (s_telemetryBridge) {
            // Peak
            auto peakFrame = Layer2::TelemetryHelpers::makePeakMeter(nodeId, s->lastPeakResult[ch], s->lastPeakResult[ch] > 1.0f);
            peakFrame.payload[2] = ch; // Channel index
            s_telemetryBridge->pushTelemetry(peakFrame);
            
            // RMS (Using manual frame creation as no helper exists for RMS specifically in v1.0)
            Layer2::ITelemetryBridge::BridgeTelemetryFrame rmsFrame = {};
            rmsFrame.sourceId = nodeId;
            rmsFrame.type = TelemetryFrame::RMS_METER;
            rmsFrame.priority = Layer2::TelemetryPriority::NORMAL;
            
            uint32_t rmsBits;
            std::memcpy(&rmsBits, &s->lastRMSResult[ch], sizeof(float));
            rmsFrame.payload[0] = rmsBits;
            rmsFrame.payload[2] = ch; // Channel index
            
            s_telemetryBridge->pushTelemetry(rmsFrame);
        }

        // 4. Output Events (Clip Detection Example)
        if (s->lastPeakResult[ch] > 1.0f && outEvents && outEventCount && *outEventCount < 64) {
            EventData& clipEvent = outEvents[(*outEventCount)++];
            clipEvent.targetNodeId = nodeId;
            clipEvent.sampleOffset = 0; // Block-level detection
            clipEvent.eventType = EventType::CUSTOM;
            clipEvent.payload.automation.parameterIndex = 0xDEADBEEF; // Custom Clip code
            clipEvent.payload.automation.targetValue = s->lastPeakResult[ch];
        }
    }

    // 5. Final Buffer Sanitization
    if (outputs) {
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            Math::Vector::sanitize(outputs[ch], numSamples);
        }
    }
}

} // namespace DSP
