// tests/unit/Middle Bridge/hardware_settings_facade_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Middle Bridge/engine/hardware_settings_facade.h"
#include "Hardware/OS abstraction/audio/iaudio_driver.h"
#include "Core audio engine/engine/iaudio_engine.h"
#include <memory>

namespace {

// Mock IAudioDriver for testing facade transitions and state queries
class MockAudioDriver : public Layer1::IAudioDriver {
public:
    Layer1::StreamState state = Layer1::StreamState::IDLE;
    Layer1::IAudioDriver::StreamConfig activeConfig;
    uint32_t stopCalls = 0;
    uint32_t closeCalls = 0;
    uint32_t openCalls = 0;
    uint32_t startCalls = 0;

    MockAudioDriver() {
        activeConfig.inputDeviceIndex = Layer1::UNUSED_DEVICE_INDEX;
        activeConfig.outputDeviceIndex = Layer1::UNUSED_DEVICE_INDEX;
        activeConfig.numInputChannels = 0;
        activeConfig.numOutputChannels = 0;
        activeConfig.sampleRate = 0;
        activeConfig.bufferSize = 0;
        activeConfig.client = nullptr;
    }

    Layer1::OpenResult openStream(const Layer1::IAudioDriver::StreamConfig& config) override {
        openCalls++;
        activeConfig = config;
        state = Layer1::StreamState::OPEN;
        return Layer1::OpenResult{.success = true};
    }

    bool startStream() override {
        startCalls++;
        state = Layer1::StreamState::RUNNING;
        return true;
    }

    bool stopStream() override {
        stopCalls++;
        state = Layer1::StreamState::OPEN;
        return true;
    }

    void closeStream() override {
        closeCalls++;
        state = Layer1::StreamState::IDLE;
    }

    Layer1::StreamState getState() const override {
        return state;
    }

    Layer1::IAudioDriver::StreamConfig getStreamConfig() const override {
        return activeConfig;
    }

    uint32_t getDeviceCount() const override {
        return 2;
    }

    Layer1::DeviceInfo getDeviceInfo(uint32_t deviceIndex) const override {
        Layer1::DeviceInfo info = {};
        if (deviceIndex == 0) {
            std::strncpy(info.name, "Mock Input Device", sizeof(info.name) - 1);
            info.maxInputChannels = 4;
            info.maxOutputChannels = 0;
            info.isDefaultInput = true;
            info.isDefaultOutput = false;
            info.defaultSampleRate = 48000;
        } else if (deviceIndex == 1) {
            std::strncpy(info.name, "Mock Output Device", sizeof(info.name) - 1);
            info.maxInputChannels = 0;
            info.maxOutputChannels = 2;
            info.isDefaultInput = false;
            info.isDefaultOutput = true;
            info.defaultSampleRate = 44100;
        }
        return info;
    }
};

// Mock IAudioEngine for testing facade interaction and performance tracking
class MockAudioEngine : public Layer3::IAudioEngine {
public:
    double mockCpuLoad = 0.35;
    uint32_t mockXrunCount = 3;
    double preparedSampleRate = 0.0;
    uint32_t preparedBlockSize = 0;
    bool prepareCalled = false;

    void prepare(double sampleRate, uint32_t maxBlockSize, Layer1::WorkgroupHandle workgroupHandle) override {
        (void)workgroupHandle;
        prepareCalled = true;
        preparedSampleRate = sampleRate;
        preparedBlockSize = maxBlockSize;
    }

    void reset() override {}

    double getCpuLoad() const override {
        return mockCpuLoad;
    }

    uint32_t getXrunCount() const override {
        return mockXrunCount;
    }

    uint32_t getTotalRTLSamples() const override { return 0; }
    uint32_t getNodePDCDelaySamples(NodeID) const override { return 0; }
    double getSampleRate() const override { return preparedSampleRate; }
    TransportState getTransportState() const override { return TransportState::STOPPED; }
    uint64_t getTransportPosition() const override { return 0; }

    // Client overrides (unused in this mock testing facade)
    void startCycle(uint64_t, uint32_t) override {}
    void processAudio(float* const*, uint32_t, float* const*, uint32_t, uint32_t) override {}
    void endCycle(uint32_t) override {}
    void onBufferSizeChanged(uint32_t) override {}
    void onSampleRateChanged(uint32_t) override {}
    void onXrun() override {}
    void onDeviceDisconnected() override {}

    void setScheduler(Layer3::IDSPKernel*) override {}
    void setTransport(Layer3::ITransport*) override {}
    void setClockService(Layer2::IClockService*) override {}
    void setMutationBridge(Layer2::IMutationBridge*) override {}
    void setEventQueue(Layer2::IEventQueue*) override {}
    void setTelemetryBridge(Layer2::ITelemetryBridge*) override {}
    void setTempoService(Layer2::ITempoService*) override {}
    void setMIDIDriver(Layer1::IMIDIDriver*) override {}
    void setFileSystem(Layer1::IFileSystem*) override {}
    void setMIDIPlayheadRenderer(IMIDIPlayheadRenderer*) override {}
    void setMidiClipDataProvider(const IMidiClipDataProvider*) override {}
    void setAutomationProcessor(Layer3::IAutomationProcessor*) override {}
    void setOfflineExportActive(bool) override {}
    Layer3::IButlerThread* getButlerThread() override { return nullptr; }
};

// Mock IMIDIDriver for testing MIDI routing configuration
class MockMidiDriver : public Layer1::IMIDIDriver {
public:
    uint32_t getDeviceCount() override { return 1; }
    uint32_t getDeviceName(uint32_t, char* outName, uint32_t maxLength) override {
        std::strncpy(outName, "Mock MIDI Port", maxLength - 1);
        outName[maxLength - 1] = '\0';
        return static_cast<uint32_t>(std::strlen(outName));
    }
    bool openInputPort(uint32_t, IMIDICallback*, uint32_t) override { return true; }
    bool closeInputPort(uint32_t) override { return true; }
    bool popMIDIEvent(Layer1::MIDIMessage&) override { return false; }
    Layer1::VirtualPortHandle createVirtualInputPort(const char*) override { return Layer1::VirtualPortHandle::invalid(); }
    bool closeVirtualPort(Layer1::VirtualPortHandle) override { return false; }
    bool sendMIDIMessage(uint32_t, const Layer1::MIDIMessage&) override { return false; }
};

} // namespace

TEST_CASE("HardwareSettingsFacade: Device Discovery & Details", "[MiddleBridge][Hardware]") {
    MockAudioDriver driver;
    MockMidiDriver midiDriver;
    MockAudioEngine engine;
    bridge::HardwareSettingsFacade facade(&driver, &midiDriver, &engine);

    auto devices = facade.getAvailableDevices();
    REQUIRE(devices.size() == 2);

    CHECK(devices[0].deviceIndex == 0);
    CHECK(std::string(devices[0].name) == "Mock Input Device");
    CHECK(devices[0].maxInputChannels == 4);
    CHECK(devices[0].maxOutputChannels == 0);
    CHECK(devices[0].isDefaultInput);
    CHECK_FALSE(devices[0].isDefaultOutput);

    CHECK(devices[1].deviceIndex == 1);
    CHECK(std::string(devices[1].name) == "Mock Output Device");
    CHECK(devices[1].maxInputChannels == 0);
    CHECK(devices[1].maxOutputChannels == 2);
    CHECK_FALSE(devices[1].isDefaultInput);
    CHECK(devices[1].isDefaultOutput);
}

TEST_CASE("HardwareSettingsFacade: Config Application Lifecycle", "[MiddleBridge][Hardware]") {
    MockAudioDriver driver;
    MockMidiDriver midiDriver;
    MockAudioEngine engine;
    bridge::HardwareSettingsFacade facade(&driver, &midiDriver, &engine);

    // Initial config is empty/unset
    auto initialConfig = facade.getCurrentConfig();
    CHECK(initialConfig.inputDeviceIndex == Layer1::UNUSED_DEVICE_INDEX);
    CHECK(initialConfig.outputDeviceIndex == Layer1::UNUSED_DEVICE_INDEX);
    CHECK(initialConfig.sampleRate == 0);

    // Set and apply new config
    bridge::HardwareConfig targetConfig;
    targetConfig.inputDeviceIndex = 0;
    targetConfig.outputDeviceIndex = 1;
    targetConfig.numInputChannels = 2;
    targetConfig.numOutputChannels = 2;
    targetConfig.sampleRate = 48000;
    targetConfig.bufferSize = 256;

    // Apply config
    bool success = facade.applyConfig(targetConfig);
    REQUIRE(success);

    // Verify correct calls were made to driver and engine
    CHECK(driver.openCalls == 1);
    CHECK(driver.startCalls == 1);
    CHECK(driver.getState() == Layer1::StreamState::RUNNING);

    CHECK(engine.prepareCalled);
    CHECK(engine.preparedSampleRate == 48000.0);
    CHECK(engine.preparedBlockSize == 256);

    // Verify current config has updated to match
    auto activeConfig = facade.getCurrentConfig();
    CHECK(activeConfig.inputDeviceIndex == 0);
    CHECK(activeConfig.outputDeviceIndex == 1);
    CHECK(activeConfig.numInputChannels == 2);
    CHECK(activeConfig.numOutputChannels == 2);
    CHECK(activeConfig.sampleRate == 48000);
    CHECK(activeConfig.bufferSize == 256);

    // Test transition when re-applying new config (should stop first, then close)
    targetConfig.bufferSize = 512;
    targetConfig.sampleRate = 96000;
    
    success = facade.applyConfig(targetConfig);
    REQUIRE(success);

    CHECK(driver.stopCalls == 1); // Stopped active running stream
    CHECK(driver.closeCalls == 1); // Closed stream
    CHECK(driver.openCalls == 2);
    CHECK(driver.startCalls == 2);
    CHECK(engine.preparedSampleRate == 96000.0);
    CHECK(engine.preparedBlockSize == 512);
}

TEST_CASE("HardwareSettingsFacade: Metrics & Latency Queries", "[MiddleBridge][Hardware]") {
    MockAudioDriver driver;
    MockMidiDriver midiDriver;
    MockAudioEngine engine;
    bridge::HardwareSettingsFacade facade(&driver, &midiDriver, &engine);

    // CPU load & Xruns mapped straight to engine getters
    CHECK(facade.getCpuLoad() == Catch::Approx(0.35));
    CHECK(facade.getXrunCount() == 3);

    // Verify latency queries
    // Initially sampleRate is 0, so latency should be 0
    CHECK(facade.getLatencyMs() == 0.0);

    // Apply valid config
    bridge::HardwareConfig targetConfig;
    targetConfig.inputDeviceIndex = 0;
    targetConfig.outputDeviceIndex = 1;
    targetConfig.numInputChannels = 2;
    targetConfig.numOutputChannels = 2;
    targetConfig.sampleRate = 48000;
    targetConfig.bufferSize = 256;
    facade.applyConfig(targetConfig);

    // Latency = roundtrip = 2 * (bufferSize / sampleRate) * 1000 = 2 * (256 / 48000) * 1000 ≈ 10.666 ms
    CHECK(facade.getLatencyMs() == Catch::Approx(10.6667).margin(0.001));
}
