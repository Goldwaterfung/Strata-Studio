#include <catch2/catch_test_macros.hpp>
#include "Core infrastructure/clock/iclock_service.h"
#include <memory>
#include <thread>
#include <cmath>

using namespace Layer2;

TEST_CASE("Clock Service Cycle Management", "[Layer2][Clock]") {
    auto clock = IClockService::create();
    REQUIRE(clock != nullptr);
    
    SECTION("Initial State") {
        CHECK(clock->getCycleId() == 0);
        CHECK(clock->getCycleStartTime() == 0);
    }
    
    SECTION("Cycle Progression") {
        clock->startCycle(1000000, 256); // 1ms, 256 frames
        CHECK(clock->getCycleId() == 1);
        CHECK(clock->getCycleStartTime() == 1000000);
        
        clock->startCycle(2000000, 256); // 2ms, 256 frames
        CHECK(clock->getCycleId() == 2);
        CHECK(clock->getCycleStartTime() == 2000000);
    }
}

TEST_CASE("Clock Service Timestamp Conversions", "[Layer2][Clock]") {
    auto clock = IClockService::create();
    clock->setSampleRate(48000.0);
    
    // 48kHz: 1 sample = 20833.33 ns
    
    SECTION("Sample to Timestamp") {
        uint64_t startNs = 1000000000; // 1 second
        clock->startCycle(startNs, 256);
        
        // Offset 0
        CHECK(clock->getTimestampForSample(0) == startNs);
        
        // Offset 100
        uint64_t expected = startNs + static_cast<uint64_t>(100 * (1e9 / 48000.0));
        CHECK(clock->getTimestampForSample(100) == expected);
    }
    
    SECTION("Timestamp to Offset") {
        uint64_t startNs = 1000000000;
        clock->startCycle(startNs, 256);
        
        // Exactly at start
        CHECK(clock->getOffsetForTimestamp(startNs) == 0);
        
        // Halfway through (128 samples later)
        // Use rounding to avoid precision issues with static_cast
        uint64_t halfPoint = startNs + static_cast<uint64_t>(std::round(128 * (1e9 / 48000.0)));
        CHECK(clock->getOffsetForTimestamp(halfPoint) == 128);
        
        // Beyond buffer (should clamp to max frames - 1)
        uint64_t wayBeyond = startNs + 10000000; // +10ms
        CHECK(clock->getOffsetForTimestamp(wayBeyond) == 255);
    }
}
