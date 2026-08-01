#pragma once
#include <cstdint>
#include "common/system_primitives.h"

namespace composition {

namespace MixerRoutingOps {
    constexpr uint32_t SET_FADER_GAIN        = 0;
    constexpr uint32_t SET_PAN               = 1;
    constexpr uint32_t SET_MUTE              = 2;
    constexpr uint32_t SET_SOLO              = 3;
    constexpr uint32_t SET_SEND_GAIN         = 4;
    constexpr uint32_t SET_SEND_PAN          = 5;
    constexpr uint32_t SET_SEND_ENABLED      = 6;
    constexpr uint32_t SET_SEND_DEST         = 7;
    constexpr uint32_t SET_AUDIO_INPUT       = 8;
    constexpr uint32_t INSERT_PLUGIN         = 9;
    constexpr uint32_t REMOVE_PLUGIN         = 10;
    constexpr uint32_t SET_PLUGIN_BYPASS     = 11;
    constexpr uint32_t INSERT_INSTRUMENT     = 12;
    constexpr uint32_t REMOVE_INSTRUMENT     = 13;
    constexpr uint32_t SET_INSTRUMENT_BYPASS = 14;
}

struct MixerParamPayload {
    TrackID trackId;
    uint32_t parameterIndex; // 0 = Volume, 1 = Pan, 2 = Mute, 3 = Solo
    float oldValue;
    float newValue;
};

struct SendRoutingPayload {
    TrackID trackId;
    uint32_t sendIndex;
    bool isPreFader;
    uint32_t parameterIndex; // 0 = Gain, 1 = Pan, 2 = Enabled, 3 = DestinationNode
    float oldValue;
    float newValue;
    NodeID oldDestNodeId;
    NodeID newDestNodeId;
};

struct PluginLifecyclePayload {
    TrackID trackId;
    uint32_t slotIndex; // 0-7, or 0xFFFFFFFF for instrument
    uint32_t pluginId;
    uint32_t operationType; // e.g. MixerRoutingOps value
    uint32_t stateId; // central non-real-time PluginStateCache ID
    bool oldBypassed;
    bool newBypassed;
};

static_assert(sizeof(MixerParamPayload) <= 256, "Payload exceeds delta buffer");
static_assert(sizeof(SendRoutingPayload) <= 256, "Payload exceeds delta buffer");
static_assert(sizeof(PluginLifecyclePayload) <= 256, "Payload exceeds delta buffer");

} // namespace composition
