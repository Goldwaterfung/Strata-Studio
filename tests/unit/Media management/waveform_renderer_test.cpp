#include <catch2/catch_test_macros.hpp>
#include "Media management/waveforms/iwaveform_renderer.h"
#include "Media management/registry/imedia_registry.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "Media management/codecs/icodec_factory.h"
#include <thread>
#include <chrono>

using namespace MediaManagement;

TEST_CASE("WaveformRenderer basic operations", "[MediaManagement][Waveforms]") {
    auto registry = IMediaRegistry::create();
    auto strings = Layer2::IStringRegistry::create();
    auto codecFactory = ICodecFactory::create();
    auto renderer = IWaveformRenderer::create(registry.get(), strings.get(), codecFactory.get());
    
    AssetInfo info{};
    info.durationSamples = 10000;
    info.sampleRate = 44100;
    info.numChannels = 2;
    info.pathId = strings->registerString("dummy.wav");
    
    MediaID mediaId = registry->registerAsset(info);
    
    SECTION("Request and retrieval") {
        WaveformHandle handle = renderer->getWaveform(mediaId, WaveformResolution::MEDIUM);
        REQUIRE(handle.isValid());
        REQUIRE(renderer->isHandleValid(handle));
        
        // Wait for generation (it's async)
        int retries = 50;
        while (!renderer->isWaveformReady(handle) && retries-- > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        REQUIRE(renderer->isWaveformReady(handle));
        uint32_t peakCount = renderer->getWaveformDataSize(handle);
        REQUIRE(peakCount > 0);
        
        std::vector<MinMaxPair> peaks(peakCount);
        std::vector<float> rms(peakCount);
        uint32_t retrieved = renderer->getWaveformData(handle, peaks.data(), rms.data(), 0, peakCount);
        REQUIRE(retrieved == peakCount);
        
        renderer->releaseWaveform(handle);
        REQUIRE_FALSE(renderer->isHandleValid(handle));
    }
    
    SECTION("Invalidation") {
        WaveformHandle handle = renderer->getWaveform(mediaId, WaveformResolution::HIGH);
        renderer->invalidateCache(mediaId);
        
        REQUIRE_FALSE(renderer->isWaveformReady(handle));
        REQUIRE_FALSE(renderer->isHandleValid(handle));
        MinMaxPair dummy;
        REQUIRE(renderer->getWaveformData(handle, &dummy, nullptr, 0, 1) == 0);
    }
}
