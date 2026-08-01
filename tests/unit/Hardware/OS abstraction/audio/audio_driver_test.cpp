#include <catch2/catch_test_macros.hpp>
#include "Hardware/OS abstraction/audio/iaudio_driver.h"
#include "Hardware/OS abstraction/audio/audio_format_converter.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>

using namespace Layer1;

// Mock Audio Client for testing callbacks
class TestAudioClient : public IAudioDriver::IAudioClient {
public:
    std::atomic<uint32_t> callbackCount{0};
    std::atomic<uint64_t> lastTimestamp{0};
    std::atomic<uint32_t> lastNumFrames{0};
    bool loopbackEnabled = false;

    void startCycle(uint64_t hardwareTimestamp, uint32_t numFrames) override {
        lastTimestamp = hardwareTimestamp;
        lastNumFrames = numFrames;
    }

    void processAudio(float* const* inputChannels,
                     uint32_t numInputChannels,
                     float* const* outputChannels,
                     uint32_t numOutputChannels,
                     uint32_t numFrames) override {
        callbackCount++;

        if (outputChannels == nullptr || numOutputChannels == 0) return;

        if (loopbackEnabled && inputChannels != nullptr && numInputChannels > 0) {
            // Simple loopback: Copy first input channel to first output channel
            if (outputChannels[0] && inputChannels[0]) {
                std::memcpy(outputChannels[0], inputChannels[0], numFrames * sizeof(float));
            }
            // Zero other output channels
            for (uint32_t i = 1; i < numOutputChannels; ++i) {
                if (outputChannels[i]) std::memset(outputChannels[i], 0, numFrames * sizeof(float));
            }
        } else {
            // Silence output
            for (uint32_t i = 0; i < numOutputChannels; ++i) {
                if (outputChannels[i]) std::memset(outputChannels[i], 0, numFrames * sizeof(float));
            }
        }
    }


    void endCycle(uint32_t numFrames) override {
        (void)numFrames;
    }

    void onBufferSizeChanged(uint32_t newBufferSize) override { (void)newBufferSize; }
    void onSampleRateChanged(uint32_t newSampleRate) override { (void)newSampleRate; }
    void onXrun() override {}
    void onDeviceDisconnected() override {}
};

TEST_CASE("Audio Device Discovery", "[Layer1][Audio]") {
    auto driver = IAudioDriver::create(AudioAPI::CORE_AUDIO);
    REQUIRE(driver != nullptr);

    SECTION("Enumeration and Metadata Validation") {
        uint32_t deviceCount = driver->getDeviceCount();
        std::cout << "\n========================================\n";
        std::cout << "  DISCOVERED AUDIO DEVICES (" << deviceCount << " found)\n";
        std::cout << "========================================\n";
        
        uint32_t defaultInputCount = 0;
        uint32_t defaultOutputCount = 0;

        for (uint32_t i = 0; i < deviceCount; ++i) {
            DeviceInfo info = driver->getDeviceInfo(i);
            std::cout << "Device [" << i << "]: " << info.name << "\n";
            std::cout << "  Manufacturer:     " << info.manufacturer << "\n";
            std::cout << "  Max Input Ch:     " << info.maxInputChannels << "\n";
            std::cout << "  Max Output Ch:    " << info.maxOutputChannels << "\n";
            std::cout << "  Default Rate:     " << info.defaultSampleRate << " Hz\n";
            std::cout << "  Preferred Buffer: " << info.preferredBufferSize << " frames\n";
            std::cout << "  Default Input:    " << (info.isDefaultInput ? "Yes" : "No") << "\n";
            std::cout << "  Default Output:   " << (info.isDefaultOutput ? "Yes" : "No") << "\n";
            std::cout << "  Supported Rates:  ";
            for (uint32_t r = 0; r < info.numSampleRates; ++r) {
                std::cout << info.supportedSampleRates[r] << (r + 1 < info.numSampleRates ? ", " : "");
            }
            std::cout << "\n----------------------------------------\n";

            REQUIRE(std::strlen(info.name) > 0);
            REQUIRE(info.numSampleRates > 0);
            REQUIRE(info.numSampleRates <= MAX_SUPPORTED_SAMPLE_RATES);

            if (info.isDefaultInput) defaultInputCount++;
            if (info.isDefaultOutput) defaultOutputCount++;
        }

        if (deviceCount > 0) {
            CHECK(defaultInputCount == 1);
            CHECK(defaultOutputCount == 1);
        }
    }
}

TEST_CASE("Audio Stream Lifecycle", "[Layer1][Audio]") {
    auto driver = IAudioDriver::create(AudioAPI::CORE_AUDIO);
    REQUIRE(driver != nullptr);

    TestAudioClient client;
    uint32_t deviceCount = driver->getDeviceCount();
    
    // Skip if no devices
    if (deviceCount == 0) return;

    // Find default devices
    uint32_t defIn = UNUSED_DEVICE_INDEX;
    uint32_t defOut = UNUSED_DEVICE_INDEX;
    for (uint32_t i = 0; i < deviceCount; ++i) {
        DeviceInfo info = driver->getDeviceInfo(i);
        if (info.isDefaultInput) defIn = i;
        if (info.isDefaultOutput) defOut = i;
    }

    IAudioDriver::StreamConfig config;
    config.inputDeviceIndex = defIn;
    config.outputDeviceIndex = defOut;
    config.numInputChannels = (defIn != UNUSED_DEVICE_INDEX) ? 1 : 0;
    config.numOutputChannels = (defOut != UNUSED_DEVICE_INDEX) ? 1 : 0;
    config.sampleRate = 48000;
    config.bufferSize = 256;
    config.client = &client;

    SECTION("Full Lifecycle: Open -> Start -> Stop -> Close") {
        // 1. Open Stream
        OpenResult result = driver->openStream(config);
        REQUIRE(result.success == true);
        REQUIRE(driver->getState() == StreamState::OPEN);

        // 2. Start Stream
        bool started = driver->startStream();
        REQUIRE(started == true);
        REQUIRE(driver->getState() == StreamState::RUNNING);

        // 3. Monitor Callbacks (wait for ~100 cycles)
        // 100 cycles at 256 samples/48kHz is ~533ms
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        uint32_t countAfterWait = client.callbackCount.load();
        
        // We expect around 100-120 callbacks. 
        // At least 50 is a safe bet for a loaded system.
        CHECK(countAfterWait >= 50);
        std::cout << "  RT Callback: " << countAfterWait << " cycles captured" << std::endl;

        // 4. Stop Stream
        bool stopped = driver->stopStream();
        REQUIRE(stopped == true);
        REQUIRE(driver->getState() == StreamState::OPEN);
        
        uint32_t countAtStop = client.callbackCount.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        REQUIRE(client.callbackCount.load() == countAtStop); // Should have stopped

        // 5. Close Stream
        driver->closeStream();
        REQUIRE(driver->getState() == StreamState::IDLE);
    }
}

TEST_CASE("Audio Format Conversion", "[Layer1][Audio]") {
    SECTION("Interleaved to Planar (Float32)") {
        const uint32_t frames = 4;
        const uint32_t channels = 2;
        float interleaved[frames * channels] = {
            1.0f, 0.5f,  // L0, R0
            0.8f, 0.4f,  // L1, R1
            0.6f, 0.3f,  // L2, R2
            0.4f, 0.2f   // L3, R3
        };

        float left[frames];
        float right[frames];
        float* planar[channels] = { left, right };

        AudioFormatConverter::interleavedToPlanar(interleaved, planar, channels, frames);

        CHECK(left[0] == 1.0f);
        CHECK(left[1] == 0.8f);
        CHECK(right[0] == 0.5f);
        CHECK(right[1] == 0.4f);
    }
}

