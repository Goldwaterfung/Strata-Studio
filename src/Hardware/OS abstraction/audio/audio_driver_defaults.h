// audio_driver_defaults.h
// Layer 1: Hardware/OS Abstraction - Unified Audio Driver Defaults

#ifndef AUDIO_DRIVER_DEFAULTS_H
#define AUDIO_DRIVER_DEFAULTS_H

#include <cstdint>
#include <vector>

namespace Layer1 {
namespace AudioDefaults {

    /**
     * Standard Sample Rate used as a fallback or default.
     */
    constexpr uint32_t SAMPLE_RATE = 44100;

    /**
     * Preferred Buffer Size (frames per buffer).
     */
    constexpr uint32_t BUFFER_SIZE = 512;

    /**
     * Default Number of Channels (Stereo).
     */
    constexpr uint32_t NUM_CHANNELS = 2;

    /**
     * Default Number of Buffers for APIs that support it (e.g., RtAudio, WASAPI).
     */
    constexpr uint32_t NUM_BUFFERS = 2;

    /**
     * Timeout for OS-level wait operations (e.g., WaitForMultipleObjects).
     */
    constexpr uint32_t WAIT_TIMEOUT_MS = 500;

    /**
     * Common Sample Rates supported by the system for enumeration.
     */
    static const uint32_t COMMON_SAMPLE_RATES[] = {
        44100, 48000, 88200, 96000, 176400, 192000
    };
    
    constexpr uint32_t NUM_COMMON_SAMPLE_RATES = sizeof(COMMON_SAMPLE_RATES) / sizeof(COMMON_SAMPLE_RATES[0]);

} // namespace AudioDefaults
} // namespace Layer1

#endif // AUDIO_DRIVER_DEFAULTS_H
