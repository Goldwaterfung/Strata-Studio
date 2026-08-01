#pragma once

#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"

// Forward declaration for Layer 3 streaming buffer and Layer 1 file system
namespace Layer3 { class IStreamingBuffer; }
namespace Layer1 { class IFileSystem; }

namespace DSP {

/**
 * @brief Sampler DSP Node State (POD)
 * 
 * Bridges Layer 3 streaming buffers to the audio graph.
 */
struct SamplerState {
    Layer3::IStreamingBuffer* buffer = nullptr;
    uint64_t playbackPosition = 0;
    bool isPlaying = false;
    bool isLooping = false;
    uint64_t loopStart = 0;
    uint64_t loopEnd = 0;
    
    float targetGain = 1.0f; // Velocity/Expression gain

    void reset() {
        buffer = nullptr;
        playbackPosition = 0;
        isPlaying = false;
        isLooping = false;
        loopStart = 0;
        loopEnd = 0;
        targetGain = 1.0f;
    }
};

/**
 * @brief Factory for creating Sampler nodes.
 */
class SamplerFactory : public BaseNodeFactory<SamplerState, 2048, NODE_TYPE_SAMPLER> {
public:
    static Layer1::IFileSystem* s_fileSystem;
    static void setFileSystem(Layer1::IFileSystem* fs) { s_fileSystem = fs; }

    NodeID createNode() override {
        auto id = BaseNodeFactory::createNode();
        if (id.isValid()) {
            if (auto* state = getRegistry().get(id)) {
                state->reset();
            }
        }
        return id;
    }

    // Specialized methods for Layer 5 to control the sampler
    void setBuffer(NodeID id, Layer3::IStreamingBuffer* buffer) {
        if (auto* s = getRegistry().get(id)) s->buffer = buffer;
    }

    void setPlaybackState(NodeID id, bool playing) {
        if (auto* s = getRegistry().get(id)) s->isPlaying = playing;
    }
};

/**
 * @brief Standardized processing function for the Sampler node.
 */
void processSampler(
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
