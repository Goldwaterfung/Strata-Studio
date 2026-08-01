// rtmidi_driver.h
// Layer 1: Hardware/OS Abstraction - RtMidi Driver Implementation
// This driver provides a cross-platform implementation using the RtMidi library.

#pragma once

#include "imidi_driver.h"
#include <memory>
#include <atomic>
#include <vector>

#include "midi_config.h"

namespace Layer1 {

class RtMidiDriver : public IMIDIDriver {
public:
    explicit RtMidiDriver(AudioAPI api);
    ~RtMidiDriver() override;

    uint32_t getDeviceCount() override;
    uint32_t getDeviceName(uint32_t deviceIndex, char* outName, uint32_t maxLength) override;

    bool openInputPort(uint32_t deviceIndex,
                      IMIDICallback* callback,
                      uint32_t queueCapacity) override;

    bool closeInputPort(uint32_t deviceIndex) override;

    bool popMIDIEvent(MIDIMessage& outMessage) override;

    VirtualPortHandle createVirtualInputPort(const char* name) override;

    bool closeVirtualPort(VirtualPortHandle handle) override;
    bool sendMIDIMessage(uint32_t deviceIndex, const MIDIMessage& message) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl;

    // RT-safe collection of queues for popMIDIEvent
    std::atomic<ILockFreeQueue<MIDIMessage>*> activeQueues[MIDI_MAX_RT_PORTS];

    // Callback for RtMidi
    static void rtMidiCallback(double timeStamp, std::vector<unsigned char>* message, void* userData);
};

} // namespace Layer1
