// direct_sound_driver.cpp
// Layer 1: Hardware/OS Abstraction - Windows DirectSound Audio Driver Implementation

#ifdef _WIN32

#include "../iaudio_driver.h"
#include <windows.h>
#include <dsound.h>
#include <vector>
#include <string>

namespace Layer1 {

class DirectSoundAudioDriver : public IAudioDriver {
public:
    DirectSoundAudioDriver() : state(StreamState::IDLE) {}
    ~DirectSoundAudioDriver() override { closeStream(); }

    OpenResult openStream(const StreamConfig& config) override {
        currentConfig = config;
        state = StreamState::OPEN;
        return {true, StreamError::NONE, ""};
    }

    bool startStream() override {
        if (state != StreamState::OPEN) return false;
        state = StreamState::RUNNING;
        return true;
    }

    bool stopStream() override {
        if (state != StreamState::RUNNING) return false;
        state = StreamState::OPEN;
        return true;
    }

    void closeStream() override {
        state = StreamState::IDLE;
    }

    StreamState getState() const override { return state; }

    uint32_t getDeviceCount() const override { return 0; }
    DeviceInfo getDeviceInfo(uint32_t deviceIndex) const override {
        DeviceInfo info = {};
        std::strncpy(info.name, "DirectSound Stub", sizeof(info.name));
        return info;
    }

private:
    StreamState state;
    StreamConfig currentConfig;
};

} // namespace Layer1

#endif // _WIN32
