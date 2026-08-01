// core_audio_driver.h
// Layer 1: Hardware/OS Abstraction - macOS Core Audio Driver Implementation
// PURE INTERFACE: Platform headers moved to implementation file (PIMPL)

#pragma once

#include "../iaudio_driver.h"
#include <memory>

namespace Layer1 {

/**
 * @brief macOS Core Audio implementation of IAudioDriver.
 */
class CoreAudioDriver : public IAudioDriver {
public:
    CoreAudioDriver();
    ~CoreAudioDriver() override;

    // === Stream Management === //
    OpenResult openStream(const StreamConfig& config) override;
    bool startStream() override;
    bool stopStream() override;
    void closeStream() override;
    StreamState getState() const override;

    // === Device Enumeration === //
    uint32_t getDeviceCount() const override;
    DeviceInfo getDeviceInfo(uint32_t deviceIndex) const override;
    
    WorkgroupHandle getWorkgroupHandle() const override;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;

    // Prevent copying
    CoreAudioDriver(const CoreAudioDriver&) = delete;
    CoreAudioDriver& operator=(const CoreAudioDriver&) = delete;
};

} // namespace Layer1
