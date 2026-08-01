// winmm_midi_driver.cpp
#ifdef _WIN32
#include "winmm_midi_driver.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <vector>

namespace Layer1 {

WinMMMIDIDriver::WinMMMIDIDriver() {
    for (uint32_t i = 0; i < MIDI_MAX_RT_PORTS; ++i) {
        activeQueues[i].store(nullptr, std::memory_order_relaxed);
    }
}

WinMMMIDIDriver::~WinMMMIDIDriver() {
    std::lock_guard<std::mutex> lock(portsMutex);
    for (auto& pair : inputPorts) {
        midiInStop(pair.second->handle);
        midiInClose(pair.second->handle);
    }
    inputPorts.clear();
    for (auto& pair : outputPorts) {
        midiOutClose(pair.second->handle);
    }
    outputPorts.clear();
}

uint32_t WinMMMIDIDriver::getDeviceCount() {
    return midiInGetNumDevs();
}

uint32_t WinMMMIDIDriver::getDeviceName(uint32_t deviceIndex, char* outName, uint32_t maxLength) {
    if (!outName || maxLength == 0) return 0;
    MIDIINCAPS caps;
    if (midiInGetDevCaps(deviceIndex, &caps, sizeof(MIDIINCAPS)) == MMSYSERR_NOERROR) {
        // Convert WCHAR to UTF-8 string
        int len = WideCharToMultiByte(CP_UTF8, 0, caps.szPname, -1, outName, static_cast<int>(maxLength), NULL, NULL);
        if (len > 0) {
            outName[len - 1] = '\0';
            return static_cast<uint32_t>(len - 1);
        }
    }
    outName[0] = '\0';
    return 0;
}

bool WinMMMIDIDriver::openInputPort(uint32_t deviceIndex, IMIDICallback* callback, uint32_t queueCapacity) {
    std::lock_guard<std::mutex> lock(portsMutex);
    
    if (inputPorts.find(deviceIndex) != inputPorts.end()) return false;

    // Create port info
    auto entry = std::make_unique<InputPort>();
    entry->callback = callback;
    entry->queue = LockFreeQueueFactory<MIDIMessage>::create(queueCapacity);
    if (!entry->queue) return false;

    HMIDIIN hMidiIn;
    // Pass the entry pointer as dwInstance for RT-safe access in callback
    MMRESULT res = midiInOpen(&hMidiIn, deviceIndex, (DWORD_PTR)&midiInCallback, (DWORD_PTR)entry.get(), CALLBACK_FUNCTION);
    if (res != MMSYSERR_NOERROR) return false;

    entry->handle = hMidiIn;
    
    // Register queue for RT path
    bool registered = false;
    for (uint32_t i = 0; i < MIDI_MAX_RT_PORTS; ++i) {
        ILockFreeQueue<MIDIMessage>* expected = nullptr;
        if (activeQueues[i].compare_exchange_strong(expected, entry->queue.get(), std::memory_order_acq_rel)) {
            registered = true;
            break;
        }
    }

    if (!registered) {
        midiInClose(hMidiIn);
        return false;
    }

    inputPorts[deviceIndex] = std::move(entry);
    midiInStart(hMidiIn);
    return true;
}

bool WinMMMIDIDriver::closeInputPort(uint32_t deviceIndex) {
    std::lock_guard<std::mutex> lock(portsMutex);
    auto it = inputPorts.find(deviceIndex);
    if (it == inputPorts.end()) return false;

    // Unregister from RT path
    for (uint32_t i = 0; i < MIDI_MAX_RT_PORTS; ++i) {
        if (activeQueues[i].load(std::memory_order_relaxed) == it->second->queue.get()) {
            activeQueues[i].store(nullptr, std::memory_order_release);
            break;
        }
    }

    midiInStop(it->second->handle);
    midiInClose(it->second->handle);
    inputPorts.erase(it);
    return true;
}

bool WinMMMIDIDriver::popMIDIEvent(MIDIMessage& outMessage) {
    for (uint32_t i = 0; i < MIDI_MAX_RT_PORTS; ++i) {
        ILockFreeQueue<MIDIMessage>* q = activeQueues[i].load(std::memory_order_acquire);
        if (q && q->pop(outMessage)) {
            return true;
        }
    }
    return false;
}

VirtualPortHandle WinMMMIDIDriver::createVirtualInputPort(const char* name) {
    // WinMM doesn't natively support creating virtual ports.
    // Usually require a driver like loopMIDI.
    // For now we return invalid but could potentially use a third-party driver if available.
    return VirtualPortHandle::invalid();
}

bool WinMMMIDIDriver::closeVirtualPort(VirtualPortHandle handle) {
    return false;
}

void CALLBACK WinMMMIDIDriver::midiInCallback(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    // dwInstance is our InputPort pointer
    InputPort* port = reinterpret_cast<InputPort*>(dwInstance);
    if (!port || !port->queue) return;

    if (wMsg == MIM_DATA) {
        MIDIMessage msg;
        // dwParam2 is timestamp in milliseconds since midiInStart
        // Convert to nanoseconds (approximate since it's from midiInStart epoch)
        msg.timestamp = static_cast<uint64_t>(dwParam2) * 1000000;
        
        // Parse MIDI status and data
        uint8_t status = (uint8_t)(dwParam1 & 0xFF);
        msg.data[0] = status;
        msg.data[1] = (uint8_t)((dwParam1 >> 8) & 0xFF);
        msg.data[2] = (uint8_t)((dwParam1 >> 16) & 0xFF);
        
        // Determine message size
        if (status < 0x80) {
            msg.size = 0; // Invalid
        } else if (status < 0xC0 || status >= 0xE0) {
            msg.size = 3;
        } else {
            msg.size = 2;
        }

        if (port->callback) {
            port->callback->onMIDIReceived(0 /* device index not easily available here */, msg, port->queue.get());
        } else {
            port->queue->push(msg);
        }
    }
}

bool WinMMMIDIDriver::sendMIDIMessage(uint32_t deviceIndex, const MIDIMessage& message) {
    if (message.size == 0) return false;

    std::lock_guard<std::mutex> lock(portsMutex);
    
    // Get or open output port
    HMIDIOUT hMidiOut = NULL;
    auto it = outputPorts.find(deviceIndex);
    if (it == outputPorts.end()) {
        if (midiOutOpen(&hMidiOut, deviceIndex, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            return false;
        }
        auto port = std::make_unique<OutputPort>();
        port->handle = hMidiOut;
        outputPorts[deviceIndex] = std::move(port);
    } else {
        hMidiOut = it->second->handle;
    }

    if (message.size <= 3) {
        // Short message (Channel voice, etc.)
        DWORD dwMsg = 0;
        dwMsg |= message.data[0];
        if (message.size >= 2) dwMsg |= (static_cast<DWORD>(message.data[1]) << 8);
        if (message.size >= 3) dwMsg |= (static_cast<DWORD>(message.data[2]) << 16);
        
        return midiOutShortMsg(hMidiOut, dwMsg) == MMSYSERR_NOERROR;
    } else {
        // Long message (SysEx)
        MIDIHDR hdr = {0};
        hdr.lpData = reinterpret_cast<LPSTR>(const_cast<uint8_t*>(message.data));
        hdr.dwBufferLength = message.size;
        hdr.dwBytesRecorded = message.size;
        hdr.dwFlags = 0;
        
        if (midiOutPrepareHeader(hMidiOut, &hdr, sizeof(MIDIHDR)) != MMSYSERR_NOERROR) {
            return false;
        }
        
        MMRESULT res = midiOutLongMsg(hMidiOut, &hdr, sizeof(MIDIHDR));
        
        // Wait for completion (not ideal for RT, but WinMM requirement for unprepare)
        // In a production DAW, we would use a dedicated output thread or buffer pool
        int timeout = 1000;
        while (!(hdr.dwFlags & MHDR_DONE) && timeout-- > 0) {
            Sleep(1);
        }
        
        midiOutUnprepareHeader(hMidiOut, &hdr, sizeof(MIDIHDR));
        return res == MMSYSERR_NOERROR;
    }
}

} // namespace Layer1

#endif // _WIN32
