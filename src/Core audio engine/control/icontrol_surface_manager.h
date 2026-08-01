// icontrol_surface_manager.h
// Layer 3: Core Audio Engine - Control Surface Manager Interface
// PURE INTERFACE: No implementation details allowed

#pragma once

#include "system_primitives.h"
#include <cstdint>
#include <memory>

namespace Layer2 {
    class IEventQueue;
}

namespace Layer1 {
    class IMIDIDriver;
}

namespace Layer3 {

//==============================================================================
// CONTROL SURFACE MANAGER INTERFACE
//==============================================================================

class IControlSurfaceManager {
public:
    //==========================================================================
    // Type Definitions
    //==========================================================================

    enum class ControlType : uint8_t {
        FADER,
        KNOB,
        BUTTON,
        ENCODER,
        PAD
    };

    struct ControlMapping {
        uint32_t surfaceId;              // Surface identifier
        uint32_t channel;                // MIDI channel (0-15)
        uint32_t controlId;              // CC number or Note
        ControlType type;                // Control type
        NodeID targetNodeId;             // Target node ID
        uint32_t targetParamIndex;       // Target parameter index
    };

    //==========================================================================
    // Factory
    //==========================================================================

    static std::unique_ptr<IControlSurfaceManager> create();

    //==========================================================================
    // Mapping Management (Non-RT-Safe)
    //==========================================================================

    // Register control mapping
    // Thread-safety: NOT RT-safe
    virtual void registerMapping(const ControlMapping& mapping) = 0;

    // Unregister control mapping
    // Thread-safety: NOT RT-safe
    virtual void unregisterMapping(uint32_t surfaceId, uint32_t controlId) = 0;

    // Commit pending mapping changes to active buffer
    // Must be called from non-RT context after registerMapping/unregisterMapping
    // Thread-safety: NOT RT-safe
    virtual void commitMappings() = 0;

    //==========================================================================
    // Input Processing (RT-Safe)
    //==========================================================================

    // Process incoming MIDI/HID events
    // Parameters:
    //   surfaceId: Surface identifier
    //   data: MIDI/HID data
    //   size: Data size in bytes
    // Thread-safety: RT-safe, wait-free
    virtual void processControlInput(uint32_t surfaceId, const uint8_t* data, uint32_t size) = 0;

    //==========================================================================
    // Feedback (Non-RT-Safe)
    //==========================================================================

    // Send feedback to surface (fader moves, LEDs)
    // Parameters:
    //   targetNodeId: Target node ID
    //   targetParamIndex: Target parameter index
    //   value: Current value
    // Thread-safety: NOT RT-safe (sends MIDI)
    virtual void sendFeedback(NodeID targetNodeId, uint32_t targetParamIndex, float value) = 0;

    //==========================================================================
    // Event Queue Attachment
    //==========================================================================

    // Attach event queue for emitting automation events
    // Must be called before processControlInput
    // Thread-safety: NOT RT-safe
    virtual void attachEventQueue(class Layer2::IEventQueue* queue) = 0;

    // Set MIDI driver for feedback output
    // Parameters:
    //   driver: MIDI driver instance
    // Thread-safety: NOT RT-safe
    virtual void setMIDIDriver(Layer1::IMIDIDriver* driver) = 0;

    //==========================================================================
    // Destructor
    //==========================================================================

    virtual ~IControlSurfaceManager() = default;
};

} // namespace Layer3
