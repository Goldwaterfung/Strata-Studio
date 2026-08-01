// control_surface_manager_impl.h
// Layer 3: Core Audio Engine - Control Surface Manager Implementation

#pragma once

#include "icontrol_surface_manager.h"
#include "Core infrastructure/bridges/ievent_queue.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace Layer3 {

//==============================================================================
// CONTROL SURFACE MANAGER IMPLEMENTATION
//==============================================================================

class ControlSurfaceManagerImpl : public IControlSurfaceManager {
public:
    //==========================================================================
    // Factory
    //==========================================================================

    static std::unique_ptr<IControlSurfaceManager> create();

    //==========================================================================
    // Construction/Destruction
    //==========================================================================

    ControlSurfaceManagerImpl();
    ~ControlSurfaceManagerImpl() override;

    // Disable copy and move
    ControlSurfaceManagerImpl(const ControlSurfaceManagerImpl&) = delete;
    ControlSurfaceManagerImpl& operator=(const ControlSurfaceManagerImpl&) = delete;
    ControlSurfaceManagerImpl(ControlSurfaceManagerImpl&&) = delete;
    ControlSurfaceManagerImpl& operator=(ControlSurfaceManagerImpl&&) = delete;

    //==========================================================================
    // IControlSurfaceManager Implementation
    //==========================================================================

    void registerMapping(const ControlMapping& mapping) override;
    void unregisterMapping(uint32_t surfaceId, uint32_t controlId) override;
    void commitMappings() override;
    void processControlInput(uint32_t surfaceId, const uint8_t* data, uint32_t size) override;
    void sendFeedback(NodeID targetNodeId, uint32_t targetParamIndex, float value) override;
    void attachEventQueue(Layer2::IEventQueue* queue) override;
    void setMIDIDriver(Layer1::IMIDIDriver* driver) override;

private:
    //==========================================================================
    // Internal Types
    //==========================================================================

    // Internal mapping storage with additional metadata
    struct MappingEntry {
        ControlMapping mapping;
        bool active;  // For marking as deleted in pending buffer
    };

    // Double-buffered mapping storage
    struct MappingBuffer {
        std::vector<MappingEntry> entries;
        uint32_t version;  // Incremented on each modification
    };

    //==========================================================================
    // Internal Helpers
    //==========================================================================

    // Find mapping in active buffer (RT-safe, no allocations)
    const IControlSurfaceManager::ControlMapping* findMapping(uint32_t surfaceId, uint32_t channelId, uint32_t controlId) const;

    // Parse MIDI message (RT-safe)
    bool parseMIDIMessage(const uint8_t* data, uint32_t size, uint8_t& outChannel, uint32_t& outControl, uint8_t& outValue, ControlType& outType) const;

    // Convert MIDI value (0-127) to normalized float (0.0-1.0)
    static float midiToNormalized(uint8_t value) {
        return static_cast<float>(value) / 127.0f;
    }

    // Convert normalized float (0.0-1.0) to MIDI value (0-127)
    static uint8_t normalizedToMIDI(float value) {
        float clamped = (value < 0.0f) ? 0.0f : (value > 1.0f) ? 1.0f : value;
        return static_cast<uint8_t>(clamped * 127.0f + 0.5f);
    }

    // Swap pending buffer to active (non-RT)
    void swapBuffers();

    //==========================================================================
    // Internal State
    //==========================================================================

    // Double-buffered mapping storage
    MappingBuffer buffer0_;
    MappingBuffer buffer1_;
    std::atomic<MappingBuffer*> activeMappings_;
    MappingBuffer* pendingMappings_;

    // Atomic flag for pending updates
    std::atomic<bool> hasPendingUpdate_;

    // Mutex for protecting pending buffer modifications
    std::mutex mappingMutex_;

    // Event queue for automation events (shared, not owned)
    Layer2::IEventQueue* eventQueue_;

    // MIDI driver for feedback (shared, not owned)
    Layer1::IMIDIDriver* midiDriver_;

    // Maximum number of mappings (pre-allocated capacity)
    static constexpr uint32_t MAX_MAPPINGS = 256;
};

} // namespace Layer3
