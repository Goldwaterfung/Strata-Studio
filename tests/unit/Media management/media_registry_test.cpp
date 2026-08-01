#include <catch2/catch_test_macros.hpp>
#include "Media management/registry/imedia_registry.h"
#include <thread>
#include <vector>

using namespace MediaManagement;

TEST_CASE("MediaRegistry basic operations", "[MediaManagement][Registry]") {
    auto registry = IMediaRegistry::create();
    REQUIRE(registry != nullptr);
    
    AssetInfo info{};
    info.sampleRate = 44100;
    info.numChannels = 2;
    info.durationSamples = 1000;
    
    SECTION("Registration and retrieval") {
        MediaID id = registry->registerAsset(info);
        REQUIRE(id.isValid());
        REQUIRE(id.generation == 1);
        
        AssetInfo retrieved{};
        REQUIRE(registry->getAssetInfo(id, retrieved));
        REQUIRE(retrieved.sampleRate == 44100);
        REQUIRE(retrieved.mediaId == id);
        REQUIRE(registry->getAssetCount() == 1);
    }
    
    SECTION("Update asset") {
        MediaID id = registry->registerAsset(info);
        
        AssetInfo updated = info;
        updated.analysis.analyzed = true;
        updated.analysis.tempo = 120.0f;
        
        REQUIRE(registry->updateAssetInfo(id, updated));
        
        AssetInfo retrieved{};
        REQUIRE(registry->getAssetInfo(id, retrieved));
        REQUIRE(retrieved.analysis.analyzed == true);
        REQUIRE(retrieved.analysis.tempo == 120.0f);
    }
    
    SECTION("Invalidation and Removal") {
        MediaID id = registry->registerAsset(info);
        REQUIRE(registry->removeAsset(id));
        REQUIRE(registry->getAssetCount() == 0);
        
        AssetInfo retrieved{};
        REQUIRE_FALSE(registry->getAssetInfo(id, retrieved));
    }
    
    SECTION("Generation counter check") {
        MediaID id = registry->registerAsset(info);
        MediaID staleId = id;
        staleId.generation = 99; // Manually invalidate
        
        AssetInfo retrieved{};
        REQUIRE_FALSE(registry->getAssetInfo(staleId, retrieved));
    }
}

TEST_CASE("MediaRegistry concurrent access", "[MediaManagement][Registry][Concurrency]") {
    auto registry = IMediaRegistry::create();
    const int numThreads = 10;
    const int iterationsPerThread = 100;
    
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};
    
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < iterationsPerThread; ++j) {
                AssetInfo info{};
                info.sampleRate = 44100u + static_cast<uint32_t>(i);
                MediaID id = registry->registerAsset(info);
                if (id.isValid()) {
                    AssetInfo retrieved{};
                    if (registry->getAssetInfo(id, retrieved)) {
                        successCount++;
                    }
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    REQUIRE(successCount == numThreads * iterationsPerThread);
    REQUIRE(registry->getAssetCount() == numThreads * iterationsPerThread);
}
