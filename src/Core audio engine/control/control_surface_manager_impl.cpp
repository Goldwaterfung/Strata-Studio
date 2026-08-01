// control_surface_manager_impl.cpp
// Layer 3: Core Audio Engine - Control Surface Manager Implementation

#include "control_surface_manager_impl.h"
#include "Hardware/OS abstraction/midi/imidi_driver.h"
#include <algorithm>
#include <cstring>
#include <cstdint>

namespace Layer3 {

//==============================================================================
// FACTORY
//==============================================================================

std::unique_ptr<IControlSurfaceManager> IControlSurfaceManager::create() {
    return ControlSurfaceManagerImpl::create();
}

std::unique_ptr<IControlSurfaceManager> ControlSurfaceManagerImpl::create() {
    return std::make_unique<ControlSurfaceManagerImpl>();
}

//==============================================================================
// CONSTRUCTION/DESTRUCTION
//==============================================================================

ControlSurfaceManagerImpl::ControlSurfaceManagerImpl()
    : hasPendingUpdate_(false)
    , eventQueue_(nullptr)
    , midiDriver_(nullptr)
{
    // Reserve capacity
    buffer0_.entries.reserve(MAX_MAPPINGS);
    buffer1_.entries.reserve(MAX_MAPPINGS);
    buffer0_.version = 0;
    buffer1_.version = 0;

    activeMappings_.store(&buffer0_, std::memory_order_release);
    pendingMappings_ = &buffer1_;
}

ControlSurfaceManagerImpl::~ControlSurfaceManagerImpl() {
    // Event queue is not owned, just clear pointer
    eventQueue_ = nullptr;
}

//==============================================================================
// MAPPING MANAGEMENT (Non-RT-Safe)
//==============================================================================

void ControlSurfaceManagerImpl::registerMapping(const ControlMapping& mapping) {
    std::lock_guard<std::mutex> lock(mappingMutex_);

    // Check if mapping already exists (include channel in the comparison)
    auto it = std::find_if(pendingMappings_->entries.begin(), pendingMappings_->entries.end(),
        [&mapping](const MappingEntry& entry) {
            return entry.active &&
                   entry.mapping.surfaceId == mapping.surfaceId &&
                   entry.mapping.channel == mapping.channel &&
                   entry.mapping.controlId == mapping.controlId;
        });

    if (it != pendingMappings_->entries.end()) {
        // Update existing mapping
        it->mapping = mapping;
    } else {
        // Add new mapping
        if (pendingMappings_->entries.size() < MAX_MAPPINGS) {
            pendingMappings_->entries.push_back({mapping, true});
        }
        // If at capacity, silently drop (could add telemetry in future)
    }

    pendingMappings_->version++;
    hasPendingUpdate_.store(true, std::memory_order_release);
}

void ControlSurfaceManagerImpl::unregisterMapping(uint32_t surfaceId, uint32_t controlId) {
    std::lock_guard<std::mutex> lock(mappingMutex_);

    // Find and deactivate mapping
    auto it = std::find_if(pendingMappings_->entries.begin(), pendingMappings_->entries.end(),
        [surfaceId, controlId](const MappingEntry& entry) {
            return entry.active &&
                   entry.mapping.surfaceId == surfaceId &&
                   entry.mapping.controlId == controlId;
        });

    if (it != pendingMappings_->entries.end()) {
        it->active = false;
        pendingMappings_->version++;
        hasPendingUpdate_.store(true, std::memory_order_release);
    }
}

void ControlSurfaceManagerImpl::commitMappings() {
    swapBuffers();
}

//==============================================================================
// INPUT PROCESSING (RT-Safe)
//==============================================================================

void ControlSurfaceManagerImpl::processControlInput(uint32_t surfaceId, const uint8_t* data, uint32_t size) {
    if (data == nullptr || size == 0 || eventQueue_ == nullptr) {
        return;
    }

    // Parse MIDI message
    uint8_t channel, value;
    uint32_t control;
    ControlType type;
    if (!parseMIDIMessage(data, size, channel, control, value, type)) {
        // Not a supported message, ignore
        return;
    }

    // Find mapping for this controller
    const IControlSurfaceManager::ControlMapping* mapping = findMapping(surfaceId, channel, control);
    if (mapping == nullptr) {
        // No mapping found, ignore
        return;
    }

    // Convert MIDI value to normalized float
    float normalizedValue = midiToNormalized(value);

    // Create automation event
    EventData event = Layer2::EventHelpers::makeAutomationEvent(
        mapping->targetNodeId,
        mapping->targetParamIndex,
        normalizedValue,
        0,  // sampleOffset - could be computed from timestamp
        0   // rampDuration - immediate change
    );

    // Push to event queue
    eventQueue_->pushEvent(event);
}

//==============================================================================
// FEEDBACK (Non-RT-Safe)
//==============================================================================

void ControlSurfaceManagerImpl::sendFeedback(NodeID targetNodeId, uint32_t targetParamIndex, float value) {
    if (midiDriver_ == nullptr) {
        return;
    }

    // Load active mappings
    MappingBuffer* active = activeMappings_.load(std::memory_order_acquire);
    if (!active) return;

    // Search for all mappings matching this target (Reverse Mapping)
    for (const auto& entry : active->entries) {
        if (entry.active &&
            entry.mapping.targetNodeId == targetNodeId &&
            entry.mapping.targetParamIndex == targetParamIndex) {
            
            // Construct MIDI message based on mapping type
            Layer1::MIDIMessage msg{};
            msg.size = 3;
            msg.deviceIndex = entry.mapping.surfaceId;

            uint8_t midiValue = normalizedToMIDI(value);

            switch (entry.mapping.type) {
                case ControlType::FADER:
                case ControlType::KNOB:
                case ControlType::ENCODER:
                    // Control Change (0xB0)
                    msg.data[0] = 0xB0 | (entry.mapping.channel & 0x0F);
                    msg.data[1] = static_cast<uint8_t>(entry.mapping.controlId & 0x7F);
                    msg.data[2] = midiValue;
                    break;

                case ControlType::BUTTON:
                case ControlType::PAD:
                    // Note On (0x90) or Note Off (0x80)
                    if (midiValue > 0) {
                        msg.data[0] = 0x90 | (entry.mapping.channel & 0x0F);
                    } else {
                        msg.data[0] = 0x80 | (entry.mapping.channel & 0x0F);
                    }
                    msg.data[1] = static_cast<uint8_t>(entry.mapping.controlId & 0x7F);
                    msg.data[2] = midiValue;
                    break;
            }

            // Send feedback
            (void)midiDriver_->sendMIDIMessage(entry.mapping.surfaceId, msg);
        }
    }
}

//==============================================================================
// EVENT QUEUE ATTACHMENT
//==============================================================================

void ControlSurfaceManagerImpl::attachEventQueue(Layer2::IEventQueue* queue) {
    eventQueue_ = queue;
}

void ControlSurfaceManagerImpl::setMIDIDriver(Layer1::IMIDIDriver* driver) {
    midiDriver_ = driver;
}

//==============================================================================
// INTERNAL HELPERS
//==============================================================================

const IControlSurfaceManager::ControlMapping* ControlSurfaceManagerImpl::findMapping(uint32_t surfaceId, uint32_t channelId, uint32_t controlId) const {
    MappingBuffer* active = activeMappings_.load(std::memory_order_acquire);
    if (!active) return nullptr;

    // Search in active mappings (RT-safe read-only access)
    for (const auto& entry : active->entries) {
        if (entry.active &&
            entry.mapping.surfaceId == surfaceId &&
            entry.mapping.channel == channelId &&
            entry.mapping.controlId == controlId) {
            return &entry.mapping;
        }
    }
    return nullptr;
}

bool ControlSurfaceManagerImpl::parseMIDIMessage(const uint8_t* data, uint32_t size, uint8_t& outChannel, uint32_t& outControl, uint8_t& outValue, ControlType& outType) const {
    if (size < 1) return false;

    uint8_t status = data[0];
    uint8_t type = status & 0xF0;
    outChannel = status & 0x0F;

    if (type == 0xB0) { // Control Change
        if (size < 3) return false;
        uint8_t cc = data[1];
        uint8_t val = data[2];

        // Handle RPN/NRPN parameter selection (CC 98/99/100/101) & high-resolution Data Entry (CC 6/38)
        if (cc == 98 || cc == 99 || cc == 100 || cc == 101) {
            // Encode RPN (0x8000) or NRPN (0x4000) prefix into 16-bit control identifier
            outControl = (cc >= 100) ? (0x8000u | static_cast<uint32_t>(val)) : (0x4000u | static_cast<uint32_t>(val));
            outValue = val;
            outType = ControlType::ENCODER;
            return true;
        }

        outControl = cc;
        outValue = val;
        outType = ControlType::KNOB; // Default to knob for standard CC
        return true;
    }
    
    if (type == 0x90 || type == 0x80) { // Note On / Note Off
        if (size < 3) return false;
        outControl = data[1];
        outValue = (type == 0x90) ? data[2] : 0;
        outType = ControlType::BUTTON;
        return true;
    }

    if (type == 0xE0) { // Pitch Bend
        if (size < 3) return false;
        // Combine two 7-bit values into 14-bit (0-16383)
        uint16_t bend = static_cast<uint16_t>((static_cast<uint32_t>(data[2]) << 7) | (data[1] & 0x7F));
        outControl = 0; // Pitch bend usually doesn't have a control ID
        outValue = static_cast<uint8_t>(bend >> 7); // Map to 7-bit for now
        outType = ControlType::ENCODER;
        return true;
    }

    // TODO: Add support for NRPN/RPN
    
    return false;
}

void ControlSurfaceManagerImpl::swapBuffers() {
    std::lock_guard<std::mutex> lock(mappingMutex_);

    // Remove inactive entries from pending buffer
    auto it = std::remove_if(pendingMappings_->entries.begin(), pendingMappings_->entries.end(),
        [](const MappingEntry& entry) { return !entry.active; });
    pendingMappings_->entries.erase(it, pendingMappings_->entries.end());

    // Atomic swap
    MappingBuffer* oldActive = activeMappings_.load(std::memory_order_acquire);
    activeMappings_.store(pendingMappings_, std::memory_order_release);
    pendingMappings_ = oldActive;

    // Copy new active entries to pending buffer so it's up to date for next mutations
    pendingMappings_->entries = activeMappings_.load(std::memory_order_relaxed)->entries;
    pendingMappings_->version = activeMappings_.load(std::memory_order_relaxed)->version + 1;

    // Clear pending update flag
    hasPendingUpdate_.store(false, std::memory_order_release);
}

} // namespace Layer3
