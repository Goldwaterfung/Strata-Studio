// rt_audio_wrapper.h
// Layer 1: Hardware/OS Abstraction - RtAudio Wrapper Header
// PURE INTERFACE: No RtAudio headers exposed

#pragma once

#include "iaudio_driver.h"
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>  // For uint32_t

namespace Layer1 {

/**
 * @brief RtAudio-based implementation of IAudioDriver
 */
class RtAudioWrapper : public IAudioDriver {
public:
    explicit RtAudioWrapper(AudioAPI api);
    ~RtAudioWrapper() override;

    // Prevent copying
    RtAudioWrapper(const RtAudioWrapper&) = delete;
    RtAudioWrapper& operator=(const RtAudioWrapper&) = delete;

    // === Stream Management === //

    OpenResult openStream(const IAudioDriver::StreamConfig& config) override;
    bool startStream() override;
    bool stopStream() override;
    void closeStream() override;
    StreamState getState() const override;
    IAudioDriver::StreamConfig getStreamConfig() const override;

    // === Device Enumeration === //

    uint32_t getDeviceCount() const override;
    DeviceInfo getDeviceInfo(uint32_t deviceIndex) const override;

private:
    class RtAudioImpl;
    friend class RtAudioImpl;

    // === Internal State (Thread-Safe) === //

    // M-4: Use mutable to allow thread-safe lazy initialization or eager initialization
    mutable std::unique_ptr<RtAudioImpl> impl;
    mutable std::mutex implMutex; // To prevent races in lazy initialization

    std::atomic<StreamState> currentState;
    const AudioAPI requestedApi;
    IAudioDriver::StreamConfig currentConfig;
    IAudioDriver::IAudioClient* clientCallback;

    // === Internal Helper Methods === //

    static int rtAudioCallback(
        void* outputBuffer,
        void* inputBuffer,
        unsigned int nFrames,
        double streamTime,
        unsigned int status, // Matches RtAudioStreamStatus
        void* userData
    );

    void processCallback(
        void* outputBuffer,
        void* inputBuffer,
        unsigned int nFrames,
        unsigned int status
    );

    OpenResult makeErrorResult(StreamError error, const char* message);
    void ensureImplInitialized() const;
};

std::unique_ptr<IAudioDriver> createRtAudioDriver(AudioAPI api);

} // namespace Layer1
