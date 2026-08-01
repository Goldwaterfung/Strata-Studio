#pragma once

#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"

namespace Layer3 { class IStreamingBuffer; }

namespace DSP {

/**
 * @brief Freeze DSP Node State (POD)
 * 
 * A stateful node that can record its input and then play it back
 * to save CPU.
 */
struct FreezeState {
    enum class Mode : uint32_t {
        Bypass = 0,     // Pass input to output
        Record = 1,     // Pass input to output AND write to buffer
        Playback = 2    // Ignore input, read from buffer
    };

    Mode mode = Mode::Bypass;
    Layer3::IStreamingBuffer* buffer = nullptr;
    uint64_t position = 0;
    uint64_t recordedLength = 0;

    void reset() {
        mode = Mode::Bypass;
        buffer = nullptr;
        position = 0;
        recordedLength = 0;
    }
};

/**
 * @brief Factory for creating Freeze nodes.
 */
class FreezeFactory : public BaseNodeFactory<FreezeState, 512, NODE_TYPE_FREEZE> {
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

/**
 * @brief Standardized processing function for the Freeze node.
 */
void processFreeze(
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
