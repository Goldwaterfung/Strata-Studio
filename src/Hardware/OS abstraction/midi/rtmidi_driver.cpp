// rtmidi_driver.cpp
// Layer 1: Hardware/OS Abstraction - RtMidi Driver Implementation

#include "rtmidi_driver.h"
#include <rtmidi/RtMidi.h>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>
#include <algorithm>

namespace Layer1 {

// Port information internal to implementation
struct PortInfo {
    std::unique_ptr<RtMidiIn> midiIn;
    IMIDIDriver::IMIDICallback* callback;
    std::unique_ptr<ILockFreeQueue<MIDIMessage>> queue;
    uint32_t deviceIndex;
    bool isVirtual;
};

struct OutputPortInfo {
    std::unique_ptr<RtMidiOut> midiOut;
    uint32_t deviceIndex;
};

// Private implementation class
class RtMidiDriver::Impl {
public:
    explicit Impl(AudioAPI api) : currentApi(api) {
        rtMidiApi = mapApi(api);
    }

    RtMidi::Api mapApi(AudioAPI api) {
        switch (api) {
            case AudioAPI::CORE_AUDIO: return RtMidi::MACOSX_CORE;
            case AudioAPI::WASAPI:
            case AudioAPI::ASIO:
            case AudioAPI::DIRECT_SOUND: return RtMidi::WINDOWS_MM;
            case AudioAPI::WINDOWS_UWP:  return RtMidi::WINDOWS_UWP;
            default: return RtMidi::UNSPECIFIED;
        }
    }

    std::map<uint32_t, std::unique_ptr<PortInfo>> physicalPorts;
    std::map<uint32_t, std::unique_ptr<PortInfo>> virtualPorts;
    std::map<uint32_t, std::unique_ptr<OutputPortInfo>> outputPorts;
    std::map<uint32_t, uint32_t> virtualPortGenerations;
    std::mutex portsMutex;
    uint32_t virtualPortCounter = 0;
    AudioAPI currentApi;
    RtMidi::Api rtMidiApi;
};

RtMidiDriver::RtMidiDriver(AudioAPI api) 
    : impl(std::make_unique<Impl>(api)) 
{
    for (uint32_t i = 0; i < MIDI_MAX_RT_PORTS; ++i) {
        activeQueues[i].store(nullptr, std::memory_order_relaxed);
    }
}

RtMidiDriver::~RtMidiDriver() {
    std::lock_guard<std::mutex> lock(impl->portsMutex);
    impl->physicalPorts.clear();
    impl->virtualPorts.clear();
    impl->outputPorts.clear();
}

uint32_t RtMidiDriver::getDeviceCount() {
    try {
        RtMidiIn midiIn(impl->rtMidiApi);
        return midiIn.getPortCount();
    } catch (...) {
        return 0;
    }
}

uint32_t RtMidiDriver::getDeviceName(uint32_t deviceIndex, char* outName, uint32_t maxLength) {
    if (!outName || maxLength == 0) return 0;
    try {
        RtMidiIn midiIn(impl->rtMidiApi);
        if (deviceIndex < midiIn.getPortCount()) {
            std::string name = midiIn.getPortName(deviceIndex);
            uint32_t copyLen = static_cast<uint32_t>(std::min(name.length(), static_cast<size_t>(maxLength - 1)));
            std::memcpy(outName, name.c_str(), copyLen);
            outName[copyLen] = '\0';
            return copyLen;
        }
    } catch (...) {}
    outName[0] = '\0';
    return 0;
}

bool RtMidiDriver::openInputPort(uint32_t deviceIndex,
                               IMIDICallback* callback,
                               uint32_t queueCapacity) {
    std::lock_guard<std::mutex> lock(impl->portsMutex);

    if (impl->physicalPorts.count(deviceIndex)) return true;

    try {
        auto midiIn = std::make_unique<RtMidiIn>(impl->rtMidiApi);
        if (deviceIndex >= midiIn->getPortCount()) return false;
        
        midiIn->ignoreTypes(false, false, false);

        auto queue = LockFreeQueueFactory<MIDIMessage>::create(queueCapacity);
        if (!queue) return false;

        auto port = std::make_unique<PortInfo>();
        port->midiIn = std::move(midiIn);
        port->callback = callback;
        port->queue = std::move(queue);
        port->deviceIndex = deviceIndex;
        port->isVirtual = false;

        port->midiIn->openPort(deviceIndex);
        
        bool registered = false;
        for (uint32_t i = 0; i < MIDI_MAX_RT_PORTS; ++i) {
            ILockFreeQueue<MIDIMessage>* expected = nullptr;
            if (activeQueues[i].compare_exchange_strong(expected, port->queue.get(), std::memory_order_acq_rel)) {
                registered = true;
                break;
            }
        }

        if (!registered) return false;

        port->midiIn->setCallback(rtMidiCallback, port.get());

        impl->physicalPorts[deviceIndex] = std::move(port);
        return true;
    } catch (RtMidiError&) {
        return false;
    }
}

bool RtMidiDriver::closeInputPort(uint32_t deviceIndex) {
    std::lock_guard<std::mutex> lock(impl->portsMutex);
    auto it = impl->physicalPorts.find(deviceIndex);
    if (it != impl->physicalPorts.end()) {
        it->second->midiIn->cancelCallback();
        it->second->midiIn->closePort();

        for (uint32_t i = 0; i < MIDI_MAX_RT_PORTS; ++i) {
            if (activeQueues[i].load(std::memory_order_relaxed) == it->second->queue.get()) {
                activeQueues[i].store(nullptr, std::memory_order_release);
                break;
            }
        }
        impl->physicalPorts.erase(it);
        return true;
    }
    return false;
}

bool RtMidiDriver::popMIDIEvent(MIDIMessage& outMessage) {
    for (uint32_t i = 0; i < MIDI_MAX_RT_PORTS; ++i) {
        ILockFreeQueue<MIDIMessage>* q = activeQueues[i].load(std::memory_order_acquire);
        if (q && q->pop(outMessage)) {
            return true;
        }
    }
    return false;
}

VirtualPortHandle RtMidiDriver::createVirtualInputPort(const char* name) {
    std::lock_guard<std::mutex> lock(impl->portsMutex);
    try {
        auto midiIn = std::make_unique<RtMidiIn>(impl->rtMidiApi);
        midiIn->ignoreTypes(false, false, false);
        midiIn->openVirtualPort(name ? name : MIDI_DEFAULT_VIRTUAL_PORT_NAME);
        
        auto queue = LockFreeQueueFactory<MIDIMessage>::create(MIDI_DEFAULT_QUEUE_CAPACITY);
        if (!queue) return VirtualPortHandle::invalid();

        uint32_t id = ++impl->virtualPortCounter;
        
        // H-2: Increment generation for this ID
        uint32_t generation = ++impl->virtualPortGenerations[id];
        if (generation == 0) generation = 1; // Avoid 0

        auto port = std::make_unique<PortInfo>();
        port->midiIn = std::move(midiIn);
        port->callback = nullptr;
        port->queue = std::move(queue);
        port->deviceIndex = id;
        port->isVirtual = true;

        bool registered = false;
        for (uint32_t i = 0; i < MIDI_MAX_RT_PORTS; ++i) {
            ILockFreeQueue<MIDIMessage>* expected = nullptr;
            if (activeQueues[i].compare_exchange_strong(expected, port->queue.get(), std::memory_order_acq_rel)) {
                registered = true;
                break;
            }
        }

        if (!registered) return VirtualPortHandle::invalid();

        port->midiIn->setCallback(rtMidiCallback, port.get());
        
        impl->virtualPorts[id] = std::move(port);
        return {id, generation};
    } catch (...) {
        return VirtualPortHandle::invalid();
    }
}

bool RtMidiDriver::closeVirtualPort(VirtualPortHandle handle) {
    std::lock_guard<std::mutex> lock(impl->portsMutex);
    if (!handle.isValid()) return false;
    
    auto it = impl->virtualPorts.find(handle.id);
    if (it != impl->virtualPorts.end()) {
        // H-2: Check generation
        if (impl->virtualPortGenerations[handle.id] != handle.generation) {
            return false;
        }

        it->second->midiIn->cancelCallback();
        
        for (uint32_t i = 0; i < MIDI_MAX_RT_PORTS; ++i) {
            if (activeQueues[i].load(std::memory_order_relaxed) == it->second->queue.get()) {
                activeQueues[i].store(nullptr, std::memory_order_release);
                break;
            }
        }
        impl->virtualPorts.erase(it);
        return true;
    }
    return false;
}

bool RtMidiDriver::sendMIDIMessage(uint32_t deviceIndex, const MIDIMessage& message) {
    std::lock_guard<std::mutex> lock(impl->portsMutex);
    
    // Check if output port is already open
    auto it = impl->outputPorts.find(deviceIndex);
    if (it == impl->outputPorts.end()) {
        try {
            auto midiOut = std::make_unique<RtMidiOut>(impl->rtMidiApi);
            if (deviceIndex >= midiOut->getPortCount()) return false;
            
            midiOut->openPort(deviceIndex);
            
            auto port = std::make_unique<OutputPortInfo>();
            port->midiOut = std::move(midiOut);
            port->deviceIndex = deviceIndex;
            
            impl->outputPorts[deviceIndex] = std::move(port);
            it = impl->outputPorts.find(deviceIndex);
        } catch (RtMidiError&) {
            return false;
        }
    }
    
    // Send message
    if (it != impl->outputPorts.end() && it->second->midiOut) {
        std::vector<unsigned char> data(message.data, message.data + message.size);
        try {
            it->second->midiOut->sendMessage(&data);
            return true;
        } catch (RtMidiError&) {
            return false;
        }
    }
    
    return false;
}

void RtMidiDriver::rtMidiCallback(double /*timeStamp*/, std::vector<unsigned char>* message, void* userData) {
    PortInfo* port = static_cast<PortInfo*>(userData);
    if (!port || !message || message->empty()) return;

    MIDIMessage msg;
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    msg.timestamp = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    
    msg.deviceIndex = port->deviceIndex;
    msg.size = static_cast<uint16_t>(std::min(static_cast<size_t>(message->size()), sizeof(msg.data)));
    std::memcpy(msg.data, message->data(), msg.size);

    if (port->callback) {
        port->callback->onMIDIReceived(port->deviceIndex, msg, port->queue.get());
    } else if (port->queue) {
        port->queue->push(msg);
    }
}

} // namespace Layer1
