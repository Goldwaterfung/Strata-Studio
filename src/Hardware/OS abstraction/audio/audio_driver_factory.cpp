// audio_driver_factory.cpp
// Layer 1: Hardware/OS Abstraction - Audio Driver Factory
// Factory pattern for platform-specific audio driver creation

#include "iaudio_driver.h"

// Include RtAudio wrapper (cross-platform implementation)
// RtAudio is the PRIMARY audio driver implementation for this project.
// It provides cross-platform support for:
// - Windows: WASAPI, ASIO, DirectSound
// - macOS: Core Audio
// - Linux: ALSA, JACK, PulseAudio
//
// The native platform-specific drivers (WASAPIAudioDriver, etc.) are
// provided for reference/testing but are disabled by default since
// RtAudio already provides production-ready implementations.
#include "rt_audio_wrapper.h"

// Optionally include platform-specific native implementations
// NOTE: These are disabled by default. Use RtAudio wrapper instead.
// If you need to use native implementations, set USE_NATIVE_AUDIO_DRIVERS=1
#if defined(_WIN32) && defined(USE_NATIVE_AUDIO_DRIVERS)
    #include "windows/wasapi_audio_driver.h"
    #include "windows/asio_audio_driver.h"
    #include "windows/direct_sound_driver.h"
#elif defined(__APPLE__) && defined(USE_NATIVE_AUDIO_DRIVERS)
    #include "macos/core_audio_driver.h"
#endif

namespace Layer1 {

std::unique_ptr<IAudioDriver> IAudioDriver::create(AudioAPI api) {
    // PRIMARY IMPLEMENTATION: RtAudio Wrapper
    //
    // RtAudio provides production-ready, cross-platform audio I/O with
    // support for all major audio APIs. This is the recommended and
    // default implementation for all platforms.
    //
    // Advantages of using RtAudio:
    // - Cross-platform consistency
    // - Well-tested and mature codebase
    // - Handles edge cases and platform quirks
    // - Regular updates and bug fixes
    // - Proper RT-safe callback implementation

#ifdef HAVE_RTAUDIO
    if (api != AudioAPI::NONE) {
        // RtAudio is our primary implementation. It handles most APIs.
        auto driver = createRtAudioDriver(api);
        if (driver) return driver;
    }
#endif

    // FALLBACK: Platform-specific native implementations
    // These are only used when RtAudio is not available or disabled.

#if defined(_WIN32) && defined(USE_NATIVE_AUDIO_DRIVERS)
    if (api == AudioAPI::WASAPI || api == AudioAPI::SHARED) {
        return std::make_unique<WASAPIAudioDriver>();
    }
    if (api == AudioAPI::ASIO) {
        return std::make_unique<ASIOAudioDriver>();
    }
    if (api == AudioAPI::DIRECT_SOUND) {
        return std::make_unique<DirectSoundAudioDriver>();
    }

#elif defined(__APPLE__) && defined(USE_NATIVE_AUDIO_DRIVERS)
    if (api == AudioAPI::CORE_AUDIO || api == AudioAPI::SHARED) {
        return std::make_unique<CoreAudioDriver>();
    }
#endif

    return nullptr;
}

} // namespace Layer1
