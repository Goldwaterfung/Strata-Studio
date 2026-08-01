// coremidi_driver.h
// Layer 1: Hardware/OS Abstraction - macOS CoreMIDI Driver Implementation

#pragma once

#include "../imidi_driver.h"
#include <CoreMIDI/CoreMIDI.h>
#include <map>
#include <mutex>
#include <vector>
#include <memory>
#include <mach/mach_time.h>

#include "../midi_config.h"

namespace Layer1 {

class CoreMIDIDriver : public IMIDIDriver {
public:
    CoreMIDIDriver();
    ~CoreMIDIDriver() override;

    uint32_t getDeviceCount() override;
    uint32_t getDeviceName(uint32_t deviceIndex,
                           char* outName,
                           uint32_t maxLength) override;

    bool openInputPort(uint32_t deviceIndex,
                      IMIDICallback* callback,
                      uint32_t queueCapacity) override;

    bool closeInputPort(uint32_t deviceIndex) override;

    bool popMIDIEvent(MIDIMessage& outMessage) override;

    VirtualPortHandle createVirtualInputPort(const char* name) override;

    bool closeVirtualPort(VirtualPortHandle handle) override;
    bool sendMIDIMessage(uint32_t deviceIndex, const MIDIMessage& message) override;
    
    const mach_timebase_info_data_t& getTimebaseInfo() const { return timebaseInfo; }

private:
    struct InputPort {
        CoreMIDIDriver* driver;
        MIDIPortRef port;
        IMIDICallback* callback;
        std::unique_ptr<ILockFreeQueue<MIDIMessage>> queue;
        uint32_t deviceIndex;
    };

    MIDIClientRef client;
    MIDIPortRef outputPort;
    std::map<uint32_t, InputPort> inputPorts;
    std::mutex portsMutex;
    mach_timebase_info_data_t timebaseInfo;

    std::atomic<ILockFreeQueue<MIDIMessage>*> activeQueues[MIDI_MAX_RT_PORTS];
    
    struct VirtualPort {
        MIDIEndpointRef endpoint;
        uint32_t generation;
    };
    std::map<uint32_t, VirtualPort> virtualPorts;
    uint32_t nextVirtualPortId = 0;
};

} // namespace Layer1
