#pragma once
#include "common/system_primitives.h" // For NodeID

namespace composition {

struct TrackSidechainBinding {
    uint32_t slotIndex{0};
    TrackID sourceTrackId{TrackID::invalid()};
    NodeID sourceOutputNode{NodeID::invalid()};
    NodeID destPluginNode{NodeID::invalid()};
    float sendGainLinear{1.0f};
    bool isEnabled{false};
};

struct TrackPipelineDescriptor {
    NodeID sourceNode;              // AudioSequencer or MIDISequencer handle
    NodeID trackNode;               // Monolithic AudioTrackNode or InstrumentTrackNode handle
    NodeID instrumentSlotNode;      // Virtual Instrument plugin handle (Instrument tracks only)
    NodeID audioInputNode;          // Hardware physical capture node
    uint32_t latencySamples{0};     // Total reported latency for PDC
    TrackSidechainBinding sidechains[8]; // Active sidechain bindings per plugin slot
    
    constexpr bool isValid() const {
        return trackNode.isValid();
    }
};

} // namespace composition
