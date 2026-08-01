#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "DSP nodes/sequencer/audio_sequencer_node.h"
#include "Core audio engine/streaming/ibutler_thread.h"
#include "Hardware/OS abstraction/filesystem/ifile_system.h"
#include <vector>
#include <cstring>

namespace {

class MockStreamingBuffer : public Layer3::IStreamingBuffer {
public:
    std::vector<float> data;
    std::vector<const float*> channelPtrs;
    uint32_t channels = 2;
    uint32_t numSamples = 1024;
    uint64_t lastRequestedRefill = 0;
    bool refillRequested = false;

    MockStreamingBuffer(uint32_t chs, uint32_t len) : channels(chs), numSamples(len) {
        data.resize(channels * numSamples, 0.0f);
        // Fill data with a simple absolute ramp to ensure R channel has different values
        for (size_t i = 0; i < data.size(); ++i) {
            data[i] = static_cast<float>(i) / static_cast<float>(numSamples);
        }
        channelPtrs.resize(channels);
        for (uint32_t ch = 0; ch < channels; ++ch) {
            channelPtrs[ch] = &data[ch * numSamples];
        }
    }

    const float* const* getRTBuffer(uint64_t readPosition) override {
        uint64_t localPos = readPosition % numSamples;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            channelPtrs[ch] = &data[ch * numSamples + localPos];
        }
        return channelPtrs.data();
    }

    void requestRefill(uint64_t readPosition) override {
        refillRequested = true;
        lastRequestedRefill = readPosition;
    }

    void provideRecordedData(const float*, uint32_t) override {}
    void associateFile(Layer1::FileHandle) override {}
    void setTimelineOffset(uint64_t, uint64_t) override {}
    void refillAsync(uint64_t, Layer1::IFileSystem*) override {}
    void flushAsync() override {}
    void setPlaybackRatio(float) override {}
    uint64_t getReadPosition() const override { return 0; }
    BufferState getState() const override { return BufferState::READY; }
    uint32_t getAvailableFrames() const override { return numSamples; }
    uint32_t getTotalCapacity() const override { return numSamples; }
    uint32_t getNumChannels() const override { return channels; }
    void setBufferSize(uint32_t) override {}
    void setReadAheadSize(uint32_t) override {}
    void setSampleRate(uint32_t) override {}
    uint32_t getSampleRate() const override { return 44100; }
};

class MockButlerThread : public Layer3::IButlerThread {
public:
    Layer3::IStreamingBuffer* mappedBuffer = nullptr;
    uint32_t mappedSourceId = 0;

    bool start(Layer1::WorkgroupHandle workgroupHandle) override { (void)workgroupHandle; return true; }
    void stop() override {}
    void setSampleRate(float) override {}
    void attachFileSystem(Layer1::IFileSystem*) override {}
    bool registerBuffer(Layer3::IStreamingBuffer*) override { return true; }
    bool unregisterBuffer(Layer3::IStreamingBuffer*) override { return true; }

    void registerBufferForRegion([[maybe_unused]] uint64_t regionId, uint32_t sourceId, Layer3::IStreamingBuffer* buffer) override {
        mappedSourceId = sourceId;
        mappedBuffer = buffer;
    }
    void unregisterBufferForRegion(uint64_t) override {
        mappedBuffer = nullptr;
        mappedSourceId = 0;
    }
    Layer3::IStreamingBuffer* getBufferForRegion([[maybe_unused]] uint64_t regionId, uint32_t sourceId) const override {
        if (sourceId == mappedSourceId) return mappedBuffer;
        return nullptr;
    }

    bool registerBufferForTrack(uint32_t, Layer3::IStreamingBuffer*) override { return true; }
    bool unregisterBufferForTrack(uint32_t) override { return true; }
    void registerSourcePath(uint32_t, const char*) override {}
    void unregisterSourcePath(uint32_t) override {}
    void updateTransportState(uint64_t, float, bool) override {}
    void updateTimelineSnapshot(const TimelineSnapshot*) override {}

    void scheduleTask(std::function<void()>) override {}
    void wakeButler() override {}
    ButlerState getState() const override { return ButlerState::IDLE; }
    uint32_t getPendingBufferCount() const override { return 0; }
};

} // namespace

TEST_CASE("AudioSequencerNode: Playback and Summing", "[DSP][AudioSequencer]") {
    DSP::AudioSequencerFactory factory;
    auto node = factory.createNode();
    REQUIRE(node.isValid());

    auto* state = factory.getRegistry().get(node);
    REQUIRE(state != nullptr);

    TrackID trackId{1, 10};
    state->trackId = trackId;
    state->targetGain = 0.5f;

    MockStreamingBuffer mockBuffer(2, 512);
    MockButlerThread mockButler;
    mockButler.registerBufferForRegion(1, 42, &mockBuffer);

    DSP::AudioSequencerFactory::setButlerThread(&mockButler);

    // Setup input/output channel arrays
    float outL[64] = {0.0f};
    float outR[64] = {0.0f};
    float* outputs[2] = { outL, outR };

    TimelineSnapshot snapshot{};
    snapshot.regionCount = 1;
    snapshot.regions[0].trackId = trackId;
    snapshot.regions[0].type = RegionType::AUDIO;
    snapshot.regions[0].sourceId = 42;
    snapshot.regions[0].positionSample = 100;
    snapshot.regions[0].sourceStartSample = 10;
    snapshot.regions[0].durationProjectFrames = 200;
    snapshot.regions[0].gain = 0.8f;
    snapshot.regions[0].isMuted = false;

    ProcessContext context{};
    context.timelineSnapshot = &snapshot;
    context.transportState = TransportState::PLAYING;
    context.transport.positionSample = 95; // region starts at 100, so first 5 samples are before region

    bool isOutputSilent = true;

    DSP::processAudioSequencer(
        node,
        nullptr,
        outputs,
        2,
        20,
        nullptr,
        0,
        nullptr,
        nullptr,
        &context,
        nullptr,
        &isOutputSilent
    );

    CHECK_FALSE(isOutputSilent);

    // First 5 samples should be silence (value = 0.0f)
    for (int i = 0; i < 5; ++i) {
        CHECK(outputs[0][i] == 0.0f);
        CHECK(outputs[1][i] == 0.0f);
    }

    // Samples 5 to 19 should be populated from the buffer
    // For sample i = 5, timelinePos = 95 + 5 = 100.
    // readIndex = (100 - 100) = 0.
    // mockBuffer data at ch L, index 0: 0 / 512 = 0.0
    // mockBuffer data at ch R, index 0: (512 + 0) / 512 = 1.0
    // Outputs should apply gain: region.gain (0.8) * state->targetGain (0.5) = 0.4
    // Expected output L: 0.0 * 0.4 = 0.0
    // Expected output R: 1.0 * 0.4 = 0.4
    CHECK(outputs[0][5] == Catch::Approx(0.0f));
    CHECK(outputs[1][5] == Catch::Approx(0.4f));

    // Verify refill was requested (the last requested refill index within the 20-sample block is 14)
    CHECK(mockBuffer.refillRequested);
    CHECK(mockBuffer.lastRequestedRefill == 14);

    factory.destroyNode(node);
}

TEST_CASE("AudioSequencerNode: Stopped or Muted Behavior", "[DSP][AudioSequencer]") {
    DSP::AudioSequencerFactory factory;
    auto node = factory.createNode();
    REQUIRE(node.isValid());

    auto* state = factory.getRegistry().get(node);
    REQUIRE(state != nullptr);

    TrackID trackId{1, 10};
    state->trackId = trackId;

    MockStreamingBuffer mockBuffer(2, 512);
    MockButlerThread mockButler;
    mockButler.registerBufferForRegion(1, 42, &mockBuffer);

    DSP::AudioSequencerFactory::setButlerThread(&mockButler);

    float outL[64] = {1.0f}; // initialized to 1.0 to check it gets cleared
    float outR[64] = {1.0f};
    float* outputs[2] = { outL, outR };

    TimelineSnapshot snapshot{};
    snapshot.regionCount = 1;
    snapshot.regions[0].trackId = trackId;
    snapshot.regions[0].type = RegionType::AUDIO;
    snapshot.regions[0].sourceId = 42;
    snapshot.regions[0].positionSample = 0;
    snapshot.regions[0].sourceStartSample = 0;
    snapshot.regions[0].durationProjectFrames = 200;
    snapshot.regions[0].gain = 1.0f;
    snapshot.regions[0].isMuted = true; // MUTED

    ProcessContext context{};
    context.timelineSnapshot = &snapshot;
    context.transportState = TransportState::PLAYING;
    context.transport.positionSample = 0;

    bool isOutputSilent = false;

    // Test muted region
    DSP::processAudioSequencer(
        node,
        nullptr,
        outputs,
        2,
        20,
        nullptr,
        0,
        nullptr,
        nullptr,
        &context,
        nullptr,
        &isOutputSilent
    );

    // Outputs should be silent because region is muted
    CHECK(isOutputSilent);
    for (int i = 0; i < 20; ++i) {
        CHECK(outputs[0][i] == 0.0f);
        CHECK(outputs[1][i] == 0.0f);
    }

    // Reset outputs
    for (int i = 0; i < 20; ++i) {
        outL[i] = 1.0f;
        outR[i] = 1.0f;
    }

    // Test transport stopped behavior
    context.transportState = TransportState::STOPPED;
    snapshot.regions[0].isMuted = false; // Unmute, but transport is stopped
    isOutputSilent = false;

    DSP::processAudioSequencer(
        node,
        nullptr,
        outputs,
        2,
        20,
        nullptr,
        0,
        nullptr,
        nullptr,
        &context,
        nullptr,
        &isOutputSilent
    );

    CHECK(isOutputSilent);
    for (int i = 0; i < 20; ++i) {
        CHECK(outputs[0][i] == 0.0f);
        CHECK(outputs[1][i] == 0.0f);
    }

    // Reset outputs
    for (int i = 0; i < 20; ++i) {
        outL[i] = 1.0f;
        outR[i] = 1.0f;
    }

    // Test out of bounds behavior (playhead beyond any region bounds)
    context.transportState = TransportState::PLAYING;
    context.transport.positionSample = 1000; // far past regionPosition + durationProjectFrames (200)
    isOutputSilent = false;

    DSP::processAudioSequencer(
        node,
        nullptr,
        outputs,
        2,
        20,
        nullptr,
        0,
        nullptr,
        nullptr,
        &context,
        nullptr,
        &isOutputSilent
    );

    CHECK(isOutputSilent);
    for (int i = 0; i < 20; ++i) {
        CHECK(outputs[0][i] == 0.0f);
        CHECK(outputs[1][i] == 0.0f);
    }

    factory.destroyNode(node);
}
