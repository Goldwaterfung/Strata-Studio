// tests/unit/Middle Bridge/waveform_cache_provider_test.cpp
#include <catch2/catch_test_macros.hpp>
#include "Middle Bridge/telemetry/waveform_cache_provider.h"
#include "Media management/waveforms/iwaveform_renderer.h"
#include <memory>
#include <unordered_map>
#include <algorithm>

using namespace bridge;
using namespace MediaManagement;

namespace {

class MockHierarchicalWaveform : public IHierarchicalWaveform {
public:
    MediaID mediaId;
    bool fullyReady = true;

    explicit MockHierarchicalWaveform(MediaID id) : mediaId(id) {}

    MediaID getMediaId() const override { return mediaId; }

    bool isAnyReady() const override { return true; }
    bool isFullyReady() const override { return fullyReady; }

    uint32_t getPeaks(uint64_t /*startSample*/, 
                      uint64_t /*endSample*/, 
                      float /*samplesPerPixel*/, 
                      MinMaxPair* outPeaks, 
                      uint32_t bufferSize) const override {
        uint32_t count = 0;
        for (uint32_t i = 0; i < bufferSize; ++i) {
            outPeaks[i].min = -0.5f;
            outPeaks[i].max = 0.5f;
            count++;
        }
        return count;
    }
};

class MockWaveformRenderer : public IWaveformRenderer {
public:
    std::unordered_map<uint32_t, std::shared_ptr<MockHierarchicalWaveform>> hierarchicalWaveforms;
    std::vector<WaveformHandle> requestedHandles;

    std::unique_ptr<IHierarchicalWaveform> getHierarchicalWaveform(MediaID mediaId) override {
        return std::make_unique<MockHierarchicalWaveform>(mediaId);
    }

    WaveformHandle getWaveform(MediaID mediaId, WaveformResolution resolution) override {
        WaveformHandle h{ mediaId.id, static_cast<uint32_t>(resolution) + 1 };
        requestedHandles.push_back(h);
        return h;
    }

    uint32_t getWaveformData(WaveformHandle /*handle*/, 
                             MinMaxPair* /*outPeakData*/, 
                             float* /*outRMSData*/,
                             uint32_t /*startFrame*/, 
                             uint32_t /*numFrames*/) const override { return 0; }

    bool isWaveformReady(WaveformHandle /*handle*/) const override { return true; }
    uint32_t getWaveformDataSize(WaveformHandle /*handle*/) const override { return 0; }
    bool isHandleValid(WaveformHandle /*handle*/) const override { return true; }
    
    void releaseWaveform(WaveformHandle handle) override {
        auto it = std::find(requestedHandles.begin(), requestedHandles.end(), handle);
        if (it != requestedHandles.end()) {
            requestedHandles.erase(it);
        }
    }

    void invalidateCache(MediaID /*mediaId*/) override {}

    std::unique_ptr<IWaveformSink> createSink(uint32_t /*sampleRate*/, uint16_t /*numChannels*/) override { return nullptr; }

    void importWaveformData(MediaID /*mediaId*/, 
                           const std::unordered_map<WaveformResolution, std::vector<MinMaxPair>>& /*peaks*/, 
                           const std::unordered_map<WaveformResolution, std::vector<float>>& /*rms*/) override {}

    void update() override {}
};

} // namespace

TEST_CASE("WaveformCacheProvider: Load, Prefetch, and Viewport", "[bridge][WaveformCacheProvider]") {
    MockWaveformRenderer renderer;
    WaveformCacheProvider provider(&renderer);

    MediaID media1{101, 1};

    SECTION("Load waveform triggers multi-resolution pre-fetching") {
        provider.requestWaveformLoad(media1);

        // Pre-fetched 5 resolutions (FULL, HIGH, MEDIUM, LOW, OVERVIEW)
        REQUIRE(renderer.requestedHandles.size() == 5);
        CHECK(renderer.requestedHandles[0].cacheId == media1.id);
        CHECK(renderer.requestedHandles[0].generation == static_cast<uint32_t>(WaveformResolution::FULL) + 1);

        // Viewport query
        WaveformSegment seg = provider.getPeakDataForViewport(media1, 0, 100000, 100);
        REQUIRE(seg.peaks != nullptr);
        REQUIRE(seg.sampleCount == 100);
        REQUIRE(seg.isLoaded == true);
        CHECK(seg.peaks[0].minVal == -0.5f);
        CHECK(seg.peaks[0].maxVal == 0.5f);

        // Release waveform cleans up handles
        provider.releaseWaveform(media1);
        CHECK(renderer.requestedHandles.empty());
    }

    SECTION("Unloaded media returns empty segment") {
        WaveformSegment seg = provider.getPeakDataForViewport(MediaID{202, 1}, 0, 10000, 100);
        CHECK(seg.peaks == nullptr);
        CHECK(seg.sampleCount == 0);
        CHECK(seg.isLoaded == false);
    }
}
