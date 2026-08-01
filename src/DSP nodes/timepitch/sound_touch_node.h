#pragma once

#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"
#include "common/math/smoothing.h"

// Forward declaration for SoundTouch
namespace soundtouch { class SoundTouch; }

namespace DSP {

/**
 * @brief SoundTouch Jitter Buffer Constants
 * 
 * We need enough space to handle extreme time stretching ratios (e.g., 0.1x)
 * and variable grain sizes from the WSOLA algorithm.
 */
constexpr uint32_t SOUNDTOUCH_JITTER_CAPACITY = 32768; // ~740ms at 44.1k

/**
 * @brief SoundTouch DSP Node State (POD Wrapper)
 * 
 * High-performance, low-latency time stretching using SoundTouch.
 */
struct SoundTouchState {
    soundtouch::SoundTouch* engine = nullptr;
    
    // Jitter buffer to smooth out variable grain sizes
    // We use a simple ring buffer structure
    float jitterBuffer[MAX_CHANNELS][SOUNDTOUCH_JITTER_CAPACITY];
    uint32_t jitterReadIdx = 0;
    uint32_t jitterWriteIdx = 0;
    uint32_t jitterLevel = 0;

    // Interleaved scratch buffer for SoundTouch (it expects interleaved data)
    // Aligned for SIMD efficiency
    alignas(32) float interleavedScratch[MAX_CHANNELS * 2048]; 

    // Parameters
    SoundTouchParams params;
    Math::ParameterSmoother ratioSmoother;

    uint32_t sampleRate = 44100;
    uint16_t numChannels = 2;
    uint32_t latency = 0;
    bool isActive = false;

    void reset() {
        engine = nullptr;
        jitterReadIdx = 0;
        jitterWriteIdx = 0;
        jitterLevel = 0;
        params.warpMode = WarpMode::BYPASS;
        params.timeRatio = 1.0f;
        params.pitchSemiTones = 0.0f;
        params.mediaBPM = 120.0f;
        ratioSmoother.init(1.0f, 20.0f, 44100.0f);
        latency = 0;
        isActive = false;
        
        // Zero the jitter buffer
        for (uint32_t ch = 0; ch < MAX_CHANNELS; ++ch) {
            std::memset(jitterBuffer[ch], 0, sizeof(float) * SOUNDTOUCH_JITTER_CAPACITY);
        }
    }
};

/**
 * @brief Factory for creating SoundTouch nodes.
 */
class SoundTouchFactory : public BaseNodeFactory<SoundTouchState, 64, NODE_TYPE_SOUNDTOUCH> {
public:
    NodeID createNode() override;
    void destroyNode(NodeID nodeId) override;
    uint32_t getLatency(NodeID nodeId) const override;
};

/**
 * @brief Standardized processing function for SoundTouch warping.
 */
void processSoundTouch(
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
