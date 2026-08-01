// asio_audio_driver.h
// Layer 1: Hardware/OS Abstraction - Windows ASIO Audio Driver

#pragma once

#include "../iaudio_driver.h"
#include <string>
#include <vector>
#include <atomic>

namespace Layer1 {

class ASIOAudioDriver : public IAudioDriver {
public:
    ASIOAudioDriver();
    ~ASIOAudioDriver() override;

    OpenResult openStream(const StreamConfig& config) override;
    bool startStream() override;
    bool stopStream() override;
    void closeStream() override;
    StreamState getState() const override;

    uint32_t getDeviceCount() const override;
    DeviceInfo getDeviceInfo(uint32_t deviceIndex) const override;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace Layer1
