// imidi_driver.h
// Layer 1: Hardware/OS Abstraction - MIDI Driver HAL Interface
// PURE INTERFACE: No platform-specific headers allowed

#pragma once

#include <cstdint>
#include <memory>
#include "../common/layer1_primitives.h"

#include "../common/lock_free_queue.h"

namespace Layer1 {

// =============================================================================
// MIDI DRIVER INTERFACE
// =============================================================================

class IMIDIDriver {
public:
    using Timestamp = uint64_t;  // Nanoseconds (high-resolution monotonic)

    // === MIDI Callback === //

    // MIDI input callback (implemented by Layer 3)
    class IMIDICallback {
    public:
        virtual ~IMIDICallback() = default;

        // Called from MIDI thread when message received
        // Implementation should push message to outQueue for audio thread
        // Thread-safety: outQueue is lock-free, safe to call from any thread
        virtual void onMIDIReceived(uint32_t deviceIndex,
                                   const MIDIMessage& message,
                                   ILockFreeQueue<MIDIMessage>* outQueue) = 0;
    };

    // === Device Enumeration === //

    // Get count of available physical MIDI input devices
    [[nodiscard]] virtual uint32_t getDeviceCount() = 0;

    // Get human-readable name of a specific MIDI input device
    // outName: Buffer to receive name (null-terminated)
    // maxLength: Size of outName buffer
    // Returns: Actual length of name (excluding null terminator)
    [[nodiscard]] virtual uint32_t getDeviceName(uint32_t deviceIndex,
                                               char* outName,
                                               uint32_t maxLength) = 0;

    // === Port Management === //

    // Open physical MIDI input device
    // callback: Invoked from MIDI thread when messages arrive
    // queueCapacity: Size of lock-free queue (must be power of 2)
    // Returns: true if port opened successfully
    [[nodiscard]] virtual bool openInputPort(uint32_t deviceIndex,
                              IMIDICallback* callback,
                              uint32_t queueCapacity) = 0;

    // Close previously opened input port
    // Precondition: Port must be open
    // Postcondition: No more callbacks will be invoked
    [[nodiscard]] virtual bool closeInputPort(uint32_t deviceIndex) = 0;

    // === Event Retrieval (Audio Thread) === //

    // Pop next MIDI event from lock-free queue (wait-free)
    // Called from audio thread during processAudio() callback
    // Returns: true if message retrieved, false if queue empty
    [[nodiscard]] virtual bool popMIDIEvent(MIDIMessage& outMessage) = 0;

    // === Virtual Ports === //

    // Create virtual input port (for inter-app MIDI)
    // Returns: Valid handle on success, invalid() on failure
    // Name is copied (caller may free after call)
    [[nodiscard]] virtual VirtualPortHandle createVirtualInputPort(const char* name) = 0;

    // Close virtual port
    // Precondition: handle must be valid
    // Postcondition: Port is destroyed, handle becomes invalid
    [[nodiscard]] virtual bool closeVirtualPort(VirtualPortHandle handle) = 0;

    // === MIDI Output === //

    // Send MIDI message to a physical output device
    // This may be called from any thread. Implementation should be RT-safe if possible.
    // Returns: true if message queued successfully
    [[nodiscard]] virtual bool sendMIDIMessage(uint32_t deviceIndex, const MIDIMessage& message) = 0;

    // === Factory === //

    static std::unique_ptr<IMIDIDriver> create(AudioAPI api);

    virtual ~IMIDIDriver() = default;
};

} // namespace Layer1
