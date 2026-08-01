// winmm_midi_driver.h
// Layer 1: Hardware/OS Abstraction - Windows WinMM MIDI Implementation

#pragma once

#ifdef _WIN32

#include "../imidi_driver.h"
#include <windows.h>
#include <mmsystem.h>
#include <map>
#include <mutex>
#include <vector>
#include <memory>
#include <atomic>

#include "../midi_config.h"

namespace Layer1 {

class WinMMMIDIDriver : public IMIDIDriver {
public:
    WinMMMIDIDriver();
    ~WinMMMIDIDriver() override;

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
    struct InputPort {
        HMIDIIN handle;
        IMIDICallback* callback;
        std::unique_ptr<ILockFreeQueue<MIDIMessage>> queue;
    };

    struct OutputPort {
        HMIDIOUT handle;
    };

    std::map<uint32_t, std::unique_ptr<InputPort>> inputPorts;
    std::map<uint32_t, std::unique_ptr<OutputPort>> outputPorts;
    std::mutex portsMutex;

    std::atomic<ILockFreeQueue<MIDIMessage>*> activeQueues[MIDI_MAX_RT_PORTS];

    static void CALLBACK midiInCallback(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2);
};

} // namespace Layer1

#endif // _WIN32
