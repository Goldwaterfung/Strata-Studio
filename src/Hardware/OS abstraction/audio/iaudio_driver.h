// iaudio_driver.h
// Layer 1: Hardware/OS Abstraction - Audio Driver HAL Interface
// PURE INTERFACE: No platform-specific headers allowed

#pragma once

#include <cstdint>
#include <memory>
#include "../common/layer1_primitives.h"

namespace Layer1 {

// =============================================================================
// AUDIO DRIVER INTERFACE
// =============================================================================

// Audio callback interface (implemented by Layer 3)
class IAudioDriver {
public:
    // =============================================================================
    // NESTED TYPES
    // =============================================================================

    // Audio callback interface (implemented by Layer 3)
    class IAudioClient {
    public:
        virtual ~IAudioClient() = default;

        // === 3-Phase "Sandwich" Pipeline Callback === //

        // Phase 1: Preparation & Clock Sync (RT-SAFE)
        // Called before processAudio to synchronize time and capture input
        virtual void startCycle(uint64_t hardwareTimestamp, uint32_t numFrames) = 0;

        // Phase 2: Primary audio processing callback (RT-SAFE)
        virtual void processAudio(float* const* inputChannels,
                                 uint32_t numInputChannels,
                                 float* const* outputChannels,
                                 uint32_t numOutputChannels,
                                 uint32_t numFrames) = 0;

        // Phase 3: Post-Processing & Safety (RT-SAFE)
        // Called after processAudio to handle safety fade-outs and cleanup
        virtual void endCycle(uint32_t numFrames) = 0;

        virtual void onBufferSizeChanged(uint32_t newBufferSize) = 0;
        virtual void onSampleRateChanged(uint32_t newSampleRate) = 0;
        virtual void onXrun() = 0;
        virtual void onDeviceDisconnected() = 0;
    };

    struct StreamConfig {
        uint32_t inputDeviceIndex;               // Index into enumerated devices
        uint32_t outputDeviceIndex;              // Index into enumerated devices
        uint32_t numInputChannels;               // Number of input channels to open
        uint32_t numOutputChannels;              // Number of output channels to open
        uint32_t sampleRate;                     // Sample rate in Hz (44100, 48000, etc.)
        uint32_t bufferSize;                     // Frames per buffer (power of 2 preferred)
        IAudioClient* client;                    // Callback interface (must outlive stream)
    };

    // === Stream Management === //

    // Open audio stream with specified configuration
    [[nodiscard]] virtual OpenResult openStream(const StreamConfig& config) = 0;

    // Start audio processing
    [[nodiscard]] virtual bool startStream() = 0;

    // Stop audio processing
    [[nodiscard]] virtual bool stopStream() = 0;

    // Close audio stream and release all resources
    virtual void closeStream() = 0;

    // Query current stream state
    virtual StreamState getState() const = 0;

    // Get current stream configuration
    virtual StreamConfig getStreamConfig() const = 0;

    // === Device Enumeration === //

    [[nodiscard]] virtual uint32_t getDeviceCount() const = 0;

    virtual DeviceInfo getDeviceInfo(uint32_t deviceIndex) const = 0;

    // === Workgroup Retrieval === //
    virtual WorkgroupHandle getWorkgroupHandle() const { return WorkgroupHandle::invalid(); }

    // === Factory === //

    static std::unique_ptr<IAudioDriver> create(AudioAPI api);

    virtual ~IAudioDriver() = default;
};

} // namespace Layer1
