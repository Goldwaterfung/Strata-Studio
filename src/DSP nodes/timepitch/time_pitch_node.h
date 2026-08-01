#pragma once

#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"
#include "common/math/smoothing.h"

// Forward declaration for RubberBand
namespace RubberBand { class RubberBandStretcher; }

namespace DSP {

/**
 * @brief Time / Pitch DSP Node State (POD Wrapper)
 * 
 * High-quality time stretching and pitch shifting using Rubber Band.
 */
struct TimePitchState {
    RubberBand::RubberBandStretcher* stretcher = nullptr;
    
    float timeRatio = 1.0f;     // 1.0 = normal, 2.0 = half speed
    float pitchShift = 0.0f;    // Semitones (e.g., 12.0 = octave up)
    
    Math::ParameterSmoother ratioSmoother;

    uint32_t latency = 0;       // Current engine latency
    bool isActive = false;

    void reset() {
        stretcher = nullptr;
        timeRatio = 1.0f;
        pitchShift = 0.0f;
        ratioSmoother.init(1.0f, 20.0f, 44100.0f); // 20ms smoothing for warping
        latency = 0;
        isActive = false;
    }
};

/**
 * @brief Factory for creating Time / Pitch nodes.
 */
class TimePitchFactory : public BaseNodeFactory<TimePitchState, 128, NODE_TYPE_TIME_PITCH> {
public:
    NodeID createNode() override;
    void destroyNode(NodeID nodeId) override;
    uint32_t getLatency(NodeID nodeId) const override;
};

/**
 * @brief Standardized processing function for Time / Pitch warping.
 */
void processTimePitch(
    NodeID nodeId,
    float* const* inputs,
    float* const* outputs,
    uint32_t numChannels,
    uint32_t numSamples,
    const EventData* events,
    uint32_t numEvents,
    EventData* outEvents,
    uint32_t* outEventCount,
    const ProcessContext* context
);

} // namespace DSP
