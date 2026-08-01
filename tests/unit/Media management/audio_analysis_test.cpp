#include <catch2/catch_test_macros.hpp>
#include "Media management/analysis/iaudio_analysis_engine.h"
#include "Media management/registry/imedia_registry.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "Media management/codecs/icodec_factory.h"
#include <thread>
#include <chrono>

using namespace MediaManagement;

struct TestContext {
    bool callbackInvoked = false;
    MediaID expectedMediaId;
};

static void testCallback(void* context, [[maybe_unused]] const AnalysisResult& result) {
    auto ctx = static_cast<TestContext*>(context);
    ctx->callbackInvoked = true;
    // Note: MediaID is no longer in AnalysisResult, 
    // so we can't verify it directly from the result POD.
}

TEST_CASE("AudioAnalysisEngine basic operations", "[MediaManagement][Analysis]") {
    auto registry = IMediaRegistry::create();
    auto strings = Layer2::IStringRegistry::create();
    auto codecFactory = ICodecFactory::create();
    auto engine = IAudioAnalysisEngine::create(registry.get(), strings.get(), codecFactory.get());
    
    AssetInfo info{};
    info.durationSamples = 44100; // 1 second
    info.sampleRate = 44100;
    info.numChannels = 2;
    info.pathId = strings->registerString("non_existent.wav");
    MediaID mediaId = registry->registerAsset(info);
    
    SECTION("Async analysis request") {
        TestContext ctx;
        ctx.expectedMediaId = mediaId;
        
        engine->analyzeAsync(mediaId, testCallback, &ctx);
        
        // Wait and update
        for (int i = 0; i < 50; ++i) {
            engine->update();
            if (ctx.callbackInvoked) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        REQUIRE(ctx.callbackInvoked);
    }
}
