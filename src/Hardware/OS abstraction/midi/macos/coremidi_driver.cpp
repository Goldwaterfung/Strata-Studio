// coremidi_driver.cpp
// Layer 1: Hardware/OS Abstraction - macOS CoreMIDI Implementation

#include "coremidi_driver.h"
#include <chrono>
#include <algorithm>

namespace Layer1 {

CoreMIDIDriver::CoreMIDIDriver() : client(0) {
    MIDIClientCreate(CFStringCreateWithCString(nullptr, MIDI_CLIENT_NAME, kCFStringEncodingUTF8), nullptr, nullptr, &client);
    
    CFStringRef outPortName = CFStringCreateWithCString(nullptr, "Output Port", kCFStringEncodingUTF8);
    MIDIOutputPortCreate(client, outPortName, &outputPort);
    CFRelease(outPortName);

    for (uint32_t i = 0; i < MIDI_MAX_RT_PORTS; ++i) {
        activeQueues[i].store(nullptr, std::memory_order_relaxed);
    }
    
    // Initialize timebase info for precise timestamping
    mach_timebase_info(&timebaseInfo);
}

uint32_t CoreMIDIDriver::getDeviceCount() {
    return static_cast<uint32_t>(MIDIGetNumberOfSources());
}

uint32_t CoreMIDIDriver::getDeviceName(uint32_t deviceIndex, char* outName, uint32_t maxLength) {
    if (!outName || maxLength == 0) return 0;
    
    MIDIEndpointRef source = MIDIGetSource(deviceIndex);
    if (source == 0) return 0;

    CFStringRef name = nullptr;
    MIDIObjectGetStringProperty(source, kMIDIPropertyName, &name);
    if (name) {
        if (CFStringGetCString(name, outName, static_cast<CFIndex>(maxLength), kCFStringEncodingUTF8)) {
            CFRelease(name);
            return static_cast<uint32_t>(std::strlen(outName));
        }
        CFRelease(name);
    }
    return 0;
}

CoreMIDIDriver::~CoreMIDIDriver() {
    std::lock_guard<std::mutex> lock(portsMutex);
    for (auto& pair : inputPorts) {
        MIDIPortDispose(pair.second.port);
    }
    for (auto& pair : virtualPorts) {
        MIDIEndpointDispose(pair.second.endpoint);
    }
    if (client) MIDIClientDispose(client);
}

bool CoreMIDIDriver::openInputPort(uint32_t deviceIndex,
                  IMIDICallback* callback,
                  uint32_t queueCapacity) {
    std::lock_guard<std::mutex> lock(portsMutex);

    if (inputPorts.count(deviceIndex)) return true;

    MIDIPortRef port;
    CFStringRef portName = CFStringCreateWithCString(nullptr, MIDI_INPUT_PORT_NAME_PREFIX, kCFStringEncodingUTF8);
    
    // Modern MIDI 1.0 Protocol (MIDI 2.0 compatible)
    OSStatus status = MIDIInputPortCreateWithProtocol(client, portName, kMIDIProtocol_1_0, &port, 
        ^void(const MIDIEventList* evtlist, void* srcConnRefCon) {
            // RT-SAFE: srcConnRefCon points directly to the InputPort struct
            InputPort* portPtr = static_cast<InputPort*>(srcConnRefCon);
            if (!portPtr || !portPtr->queue) return;
            
            ILockFreeQueue<MIDIMessage>* queue = portPtr->queue.get();
            IMIDICallback* userCallback = portPtr->callback;
            uint32_t devIndex = portPtr->deviceIndex;

            const MIDIEventPacket* packet = &evtlist->packet[0];
            for (uint32_t i = 0; i < evtlist->numPackets; ++i) {
                MIDIMessage msg;
                
                // Optimized Mach time conversion
                msg.timestamp = (packet->timeStamp * portPtr->driver->getTimebaseInfo().numer) / 
                                portPtr->driver->getTimebaseInfo().denom;
                msg.deviceIndex = devIndex;

                // Extract bytes from 32-bit UMP words (MIDI 1.0 over UMP)
                msg.size = 0;
                for (uint32_t w = 0; w < packet->wordCount && msg.size < sizeof(msg.data) - 4; ++w) {
                    uint32_t word = packet->words[w];
                    // MIDI 1.0 UMP packets are 1 word (32-bit)
                    // Bytes 2, 3, 4 contain the MIDI message
                    msg.data[msg.size++] = static_cast<uint8_t>((word >> 16) & 0xFF);
                    msg.data[msg.size++] = static_cast<uint8_t>((word >> 8) & 0xFF);
                    msg.data[msg.size++] = static_cast<uint8_t>(word & 0xFF);
                }

                if (userCallback) {
                    userCallback->onMIDIReceived(devIndex, msg, queue);
                } else {
                    queue->push(msg);
                }
                
                packet = MIDIEventPacketNext(packet);
            }
        });
    
    CFRelease(portName);
    if (status != noErr) return false;

    MIDIEndpointRef source = MIDIGetSource(deviceIndex);
    if (source == 0) {
        MIDIPortDispose(port);
        return false;
    }

    InputPort entry;
    entry.driver = this;
    entry.port = port;
    entry.callback = callback;
    entry.queue = LockFreeQueueFactory<MIDIMessage>::create(queueCapacity);
    entry.deviceIndex = deviceIndex;
    
    // Register queue for RT path
    bool registered = false;
    for (uint32_t i = 0; i < MIDI_MAX_RT_PORTS; ++i) {
        ILockFreeQueue<MIDIMessage>* expected = nullptr;
        if (activeQueues[i].compare_exchange_strong(expected, entry.queue.get(), std::memory_order_acq_rel)) {
            registered = true;
            break;
        }
    }

    if (!registered) {
        MIDIPortDispose(port);
        return false;
    }

    inputPorts[deviceIndex] = std::move(entry);
    
    // Pass the specific InputPort pointer as refCon for RT-safe access in callback
    MIDIPortConnectSource(port, source, &inputPorts[deviceIndex]);

    return true;
}

bool CoreMIDIDriver::closeInputPort(uint32_t deviceIndex) {
    std::lock_guard<std::mutex> lock(portsMutex);
    auto it = inputPorts.find(deviceIndex);
    if (it != inputPorts.end()) {
        // Unregister from RT path
        for (uint32_t i = 0; i < MIDI_MAX_RT_PORTS; ++i) {
            if (activeQueues[i].load(std::memory_order_relaxed) == it->second.queue.get()) {
                activeQueues[i].store(nullptr, std::memory_order_release);
                break;
            }
        }

        MIDIPortDispose(it->second.port);
        inputPorts.erase(it);
        return true;
    }
    return false;
}

bool CoreMIDIDriver::popMIDIEvent(MIDIMessage& outMessage) {
    // RT-SAFE: No mutex
    for (uint32_t i = 0; i < MIDI_MAX_RT_PORTS; ++i) {
        ILockFreeQueue<MIDIMessage>* q = activeQueues[i].load(std::memory_order_acquire);
        if (q && q->pop(outMessage)) {
            return true;
        }
    }
    return false;
}

VirtualPortHandle CoreMIDIDriver::createVirtualInputPort(const char* name) {
    std::lock_guard<std::mutex> lock(portsMutex);
    MIDIEndpointRef endpoint;
    CFStringRef cfName = CFStringCreateWithCString(nullptr, name ? name : MIDI_DEFAULT_VIRTUAL_PORT_NAME, kCFStringEncodingUTF8);
    
    OSStatus status = MIDIDestinationCreateWithProtocol(client, cfName, kMIDIProtocol_1_0, &endpoint, 
        ^void(const MIDIEventList* /*evtlist*/, void* /*srcConnRefCon*/) {
            // Virtual port callback logic (similar to physical port)
        });
        
    CFRelease(cfName);
    
    if (status == noErr) {
        uint32_t id = nextVirtualPortId++;
        uint32_t generation = 1;
        virtualPorts[id] = {endpoint, generation};
        return {id, generation};
    }
    return VirtualPortHandle::invalid();
}

bool CoreMIDIDriver::closeVirtualPort(VirtualPortHandle handle) {
    std::lock_guard<std::mutex> lock(portsMutex);
    if (!handle.isValid()) return false;
    
    auto it = virtualPorts.find(handle.id);
    if (it != virtualPorts.end() && it->second.generation == handle.generation) {
        MIDIEndpointDispose(it->second.endpoint);
        virtualPorts.erase(it);
        return true;
    }
    return false;
}

bool CoreMIDIDriver::sendMIDIMessage(uint32_t deviceIndex, const MIDIMessage& message) {
    if (message.size == 0) return false;

    MIDIEndpointRef dest = MIDIGetDestination(deviceIndex);
    if (dest == 0) return false;

    // Use a fixed buffer for the event list to avoid allocation
    // MIDIEventList can be small for a single MIDI 1.0 message
    alignas(MIDIEventList) uint8_t buffer[64];
    MIDIEventList* eventList = reinterpret_cast<MIDIEventList*>(buffer);
    
    // MIDI 1.0 over UMP (Universal MIDI Packet)
    // Word format: [4-bit Type | 4-bit Group | 8-bit Status | 8-bit Data1 | 8-bit Data2]
    uint32_t word = 0;
    
    // Message Type 0x2 is for MIDI 1.0 Voice Channel Messages or System Messages
    word |= (0x2 << 28);
    
    // We assume Group 0 for now
    word |= (0x0 << 24);
    
    // Copy MIDI bytes into UMP word
    if (message.size >= 1) word |= (static_cast<uint32_t>(message.data[0]) << 16);
    if (message.size >= 2) word |= (static_cast<uint32_t>(message.data[1]) << 8);
    if (message.size >= 3) word |= static_cast<uint32_t>(message.data[2]);
    
    MIDIEventPacket* packet = MIDIEventListInit(eventList, kMIDIProtocol_1_0);
    if (!packet) return false;

    packet = MIDIEventListAdd(eventList, sizeof(buffer), packet, 0 /* immediate */, 1, &word);
    
    if (packet) {
        OSStatus status = MIDISendEventList(outputPort, dest, eventList);
        return status == noErr;
    }
    
    return false;
}

} // namespace Layer1
