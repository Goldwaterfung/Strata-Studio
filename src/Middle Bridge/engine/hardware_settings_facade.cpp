// src/Middle Bridge/hardware_settings_facade.cpp
#include "engine/hardware_settings_facade.h"
#include "project/isession_manager.h"
#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include <cstring>
#include <algorithm>

namespace bridge {

HardwareSettingsFacade::HardwareSettingsFacade(
    Layer1::IAudioDriver* audioDriver,
    Layer1::IMIDIDriver* midiDriver,
    Layer3::IAudioEngine* audioEngine,
    ISessionManager* sessionManager)
    : audioDriver_(audioDriver)
    , midiDriver_(midiDriver)
    , audioEngine_(audioEngine)
    , sessionManager_(sessionManager)
{
}

std::vector<AudioDeviceDescriptor> HardwareSettingsFacade::getAvailableDevices() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AudioDeviceDescriptor> devices;
    if (!audioDriver_) return devices;

    uint32_t deviceCount = audioDriver_->getDeviceCount();
    devices.reserve(deviceCount);

    for (uint32_t i = 0; i < deviceCount; ++i) {
        Layer1::DeviceInfo info = audioDriver_->getDeviceInfo(i);
        AudioDeviceDescriptor desc;
        desc.deviceIndex = i;
        std::strncpy(desc.name, info.name, sizeof(desc.name) - 1);
        desc.name[sizeof(desc.name) - 1] = '\0';
        desc.maxInputChannels = info.maxInputChannels;
        desc.maxOutputChannels = info.maxOutputChannels;
        desc.isDefaultInput = info.isDefaultInput;
        desc.isDefaultOutput = info.isDefaultOutput;
        devices.push_back(desc);
    }

    return devices;
}

HardwareConfig HardwareSettingsFacade::getCurrentConfig() {
    std::lock_guard<std::mutex> lock(mutex_);
    HardwareConfig config = {};
    if (!audioDriver_) return config;

    // Retrieve active stream config from the low-level driver
    Layer1::IAudioDriver::StreamConfig driverConfig = audioDriver_->getStreamConfig();
    config.inputDeviceIndex = driverConfig.inputDeviceIndex;
    config.outputDeviceIndex = driverConfig.outputDeviceIndex;
    config.numInputChannels = driverConfig.numInputChannels;
    config.numOutputChannels = driverConfig.numOutputChannels;
    config.sampleRate = driverConfig.sampleRate;
    config.bufferSize = driverConfig.bufferSize;

    return config;
}

bool HardwareSettingsFacade::applyConfig(const HardwareConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!audioDriver_ || !audioEngine_) return false;

    // 1. Stop active stream if currently running
    if (audioDriver_->getState() == Layer1::StreamState::RUNNING) {
        bool stopped = audioDriver_->stopStream();
        (void)stopped;
    }

    // 2. Close active stream if currently open
    if (audioDriver_->getState() != Layer1::StreamState::IDLE) {
        audioDriver_->closeStream();
    }

    // 3. Prepare the audio engine for new parameters
    audioEngine_->prepare(config.sampleRate, config.bufferSize);

    // 3b. Propagate new sample rate across active session and track manager
    if (sessionManager_ && sessionManager_->getActiveSession()) {
        auto* session = sessionManager_->getActiveSession();
        auto metadata = session->getMetadata();
        metadata.sampleRate = config.sampleRate;
        session->setMetadata(metadata);
        if (auto* trackManager = session->getTrackManager()) {
            trackManager->setProjectSampleRate(config.sampleRate);
        }
    }

    // 4. Open audio stream with new configuration
    Layer1::IAudioDriver::StreamConfig driverConfig = {};
    driverConfig.inputDeviceIndex = config.inputDeviceIndex;
    driverConfig.outputDeviceIndex = config.outputDeviceIndex;
    driverConfig.numInputChannels = config.numInputChannels;
    driverConfig.numOutputChannels = config.numOutputChannels;
    driverConfig.sampleRate = config.sampleRate;
    driverConfig.bufferSize = config.bufferSize;
    driverConfig.client = audioEngine_;

    auto result = audioDriver_->openStream(driverConfig);
    if (!result.success) {
        return false;
    }

    // 5. Start audio stream processing
    return audioDriver_->startStream();
}

double HardwareSettingsFacade::getCpuLoad() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!audioEngine_) return 0.0;
    return audioEngine_->getCpuLoad();
}

double HardwareSettingsFacade::getLatencyMs() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!audioDriver_) return 0.0;

    Layer1::IAudioDriver::StreamConfig driverConfig = audioDriver_->getStreamConfig();
    if (driverConfig.sampleRate == 0) return 0.0;

    // Latency = roundtrip buffering latency = (2.0 * bufferSize / sampleRate) * 1000.0
    double bufferDurationMs = (static_cast<double>(driverConfig.bufferSize) / static_cast<double>(driverConfig.sampleRate)) * 1000.0;
    return 2.0 * bufferDurationMs;
}

uint32_t HardwareSettingsFacade::getXrunCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!audioEngine_) return 0;
    return audioEngine_->getXrunCount();
}

std::vector<MidiPortDescriptor> HardwareSettingsFacade::getAvailableMidiPorts() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<MidiPortDescriptor> ports;
    if (!midiDriver_) return ports;

    uint32_t portCount = midiDriver_->getDeviceCount();
    ports.reserve(portCount);

    for (uint32_t i = 0; i < portCount; ++i) {
        MidiPortDescriptor desc;
        desc.portIndex = i;
        (void)midiDriver_->getDeviceName(i, desc.name, sizeof(desc.name) - 1);
        desc.isOpen = openMidiPorts_.count(i) > 0;
        ports.push_back(desc);
    }

    return ports;
}

bool HardwareSettingsFacade::isMidiPortOpen(uint32_t portIndex) {
    std::lock_guard<std::mutex> lock(mutex_);
    return openMidiPorts_.count(portIndex) > 0;
}

bool HardwareSettingsFacade::setMidiPortOpen(uint32_t portIndex, bool open) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!midiDriver_) return false;

    if (open) {
        if (openMidiPorts_.count(portIndex) > 0) return true;
        // Open port with 1024 queue capacity
        bool success = midiDriver_->openInputPort(portIndex, nullptr, 1024);
        if (success) {
            openMidiPorts_.insert(portIndex);
            return true;
        }
        return false;
    } else {
        if (openMidiPorts_.count(portIndex) == 0) return true;
        bool success = midiDriver_->closeInputPort(portIndex);
        if (success) {
            openMidiPorts_.erase(portIndex);
            return true;
        }
        return false;
    }
}

} // namespace bridge
