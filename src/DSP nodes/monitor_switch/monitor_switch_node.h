#pragma once

#include "common/system_primitives.h"
#include "common/dsp/node_infrastructure.h"
#include "common/dsp/node_types.h"
#include <cstring>

namespace DSP {

struct MonitorSwitchState {
    uint8_t monitorState;  // cast from MonitorState (0=OFF, 1=ON, 2=AUTO)
    uint8_t reserved[7];   // Pads to 8 bytes to maintain alignment and POD layout

    void reset() {
        monitorState = 0; // Default to OFF
        std::memset(reserved, 0, sizeof(reserved));
    }
};

static_assert(std::is_pod<MonitorSwitchState>::value, "MonitorSwitchState must be Plain Old Data");

// processMonitorSwitch():
//   Inputs: inputs[0..1] represents physical audio input (from AudioInputNode),
//           inputs[2..3] represents internal track playback (from Sampler/Synth).
//   Outputs: outputs[0..1] is the resolved signal.
//   Logic:
//     - if monitorState == ON (1): output = physical input (inputs[0..1])
//     - if monitorState == OFF (0): output = track playback (inputs[2..3])
//     - if monitorState == AUTO (2):
//         - if transportState is RECORDING: output = physical input (inputs[0..1])
//         - otherwise: output = sum of physical input + track playback (inputs[0..1] + inputs[2..3])
//   RT-safe, wait-free, bounded execution time, SIMD sum if enabled.
void processMonitorSwitch(
    NodeID nodeId, float* const* inputs, float* const* outputs,
    uint32_t numChannels, uint32_t numSamples,
    const EventData* events, uint32_t numEvents, EventData* outEvents, uint32_t* outEventCount,
    const ProcessContext* context, const bool* inputSilence, bool* isOutputSilent);

class MonitorSwitchFactory
    : public BaseNodeFactory<MonitorSwitchState, 64, NODE_TYPE_MONITOR_SWITCH> {
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

} // namespace DSP
