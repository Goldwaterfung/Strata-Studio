// src/Middle Bridge/ihardware_settings_facade.h
#pragma once

#include <vector>
#include <cstdint>

namespace bridge {

struct AudioDeviceDescriptor {
    uint32_t deviceIndex = 0xFFFFFFFF;
    char name[256] = "";
    uint32_t maxInputChannels = 0;
    uint32_t maxOutputChannels = 0;
    bool isDefaultInput = false;
    bool isDefaultOutput = false;
};

struct HardwareConfig {
    uint32_t inputDeviceIndex = 0xFFFFFFFF;
    uint32_t outputDeviceIndex = 0xFFFFFFFF;
    uint32_t numInputChannels = 2;
    uint32_t numOutputChannels = 2;
    uint32_t sampleRate = 48000;
    uint32_t bufferSize = 256;
};

struct MidiPortDescriptor {
    uint32_t portIndex = 0xFFFFFFFF;
    char name[256] = "";
    bool isOpen = false;
};

class IHardwareSettingsFacade {
public:
    virtual ~IHardwareSettingsFacade() = default;

    virtual std::vector<AudioDeviceDescriptor> getAvailableDevices() = 0;
    virtual HardwareConfig getCurrentConfig() = 0;
    virtual bool applyConfig(const HardwareConfig& config) = 0;
    virtual double getCpuLoad() = 0;
    virtual double getLatencyMs() = 0;
    virtual uint32_t getXrunCount() = 0;

    virtual std::vector<MidiPortDescriptor> getAvailableMidiPorts() = 0;
    virtual bool isMidiPortOpen(uint32_t portIndex) = 0;
    virtual bool setMidiPortOpen(uint32_t portIndex, bool open) = 0;
};

} // namespace bridge
