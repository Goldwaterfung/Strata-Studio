// wasapi_audio_driver.h
// Layer 1: Hardware/OS Abstraction - Windows WASAPI Audio Driver Implementation
// PURE INTERFACE: Platform headers moved to implementation file (PIMPL)

#pragma once

#include "../iaudio_driver.h"
#include <memory>

namespace Layer1 {

/**
 * @brief Windows WASAPI implementation of IAudioDriver.
 * 
 * This class uses the PIMPL pattern to hide Windows-specific headers (windows.h, mmdeviceapi.h, etc.)
 * from the rest of the application, ensuring Layer 1 architectural purity.
 */
class WASAPIAudioDriver : public IAudioDriver {
public:
    WASAPIAudioDriver();
    ~WASAPIAudioDriver() override;

    // === Stream Management === //

    OpenResult openStream(const StreamConfig& config) override;
    bool startStream() override;
    bool stopStream() override;
    void closeStream() override;
    StreamState getState() const override;

    // === Device Enumeration === //

    uint32_t getDeviceCount() const override;
    DeviceInfo getDeviceInfo(uint32_t deviceIndex) const override;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;

    // Prevent copying
    WASAPIAudioDriver(const WASAPIAudioDriver&) = delete;
    WASAPIAudioDriver& operator=(const WASAPIAudioDriver&) = delete;
};

} // namespace Layer1
