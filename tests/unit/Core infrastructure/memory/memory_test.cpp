#include <catch2/catch_test_macros.hpp>
#include "Core infrastructure/memory/imemory_coordinator.h"
#include "Core infrastructure/memory/priority_pool.h"
#include <vector>
#include <thread>

using namespace Layer2;

TEST_CASE("MemoryCoordinator Basic Allocation", "[Layer2][Memory]") {
    IMemoryCoordinator::PoolConfig config;
    config.numBuffers = 10;
    config.initialChannels = 2;
    config.initialFramesPerBuffer = 256;
    config.alignment = 64;
    config.enableMemoryLocking = false;
    config.enablePriorityInheritance = false;
    config.realtimeBufferRatio = 50;  // 5 buffers
    config.highBufferRatio = 30;      // 3 buffers
    config.normalBufferRatio = 15;    // 1 buffer
    // BACKGROUND gets 5% -> 1 buffer (total 10)

    auto coordinator = IMemoryCoordinator::create(config);
    REQUIRE(coordinator != nullptr);
    REQUIRE(coordinator->isValid());

    SECTION("Successful Allocation") {
        auto handle = coordinator->acquireBuffer(2, 256, IMemoryCoordinator::MemoryPriority::REALTIME);
        CHECK(handle.isValid());
        CHECK(handle->bufferId < 10);
        CHECK(handle->numChannels == 2);
        CHECK(handle->numFrames == 256);
    }

    SECTION("Release and Re-acquire") {
        {
            auto handle = coordinator->acquireBuffer(2, 256, IMemoryCoordinator::MemoryPriority::REALTIME);
            CHECK(handle.isValid());
        } // handle released here
        
        auto handle2 = coordinator->acquireBuffer(2, 256, IMemoryCoordinator::MemoryPriority::REALTIME);
        CHECK(handle2.isValid());
    }

    SECTION("Pool Exhaustion") {
        std::vector<AudioBufferHandle> handles;
        // Acquire all 5 REALTIME buffers
        for (int i = 0; i < 5; ++i) {
            handles.push_back(coordinator->acquireBuffer(2, 256, IMemoryCoordinator::MemoryPriority::REALTIME));
            CHECK(handles.back().isValid());
        }
        
        // 6th REALTIME allocation should fail if inheritance is off
        auto handleFail = coordinator->acquireBuffer(2, 256, IMemoryCoordinator::MemoryPriority::REALTIME);
        CHECK(!handleFail.isValid());
    }
}

TEST_CASE("Priority Borrowing", "[Layer2][Memory]") {
    IMemoryCoordinator::PoolConfig config;
    config.numBuffers = 10;
    config.initialChannels = 2;
    config.initialFramesPerBuffer = 256;
    config.alignment = 64;
    config.enableMemoryLocking = false;
    config.enablePriorityInheritance = true; // Enable inheritance
    config.realtimeBufferRatio = 50;  // 5
    config.highBufferRatio = 30;      // 3
    config.normalBufferRatio = 10;    // 1
    // BACKGROUND -> 1

    auto coordinator = IMemoryCoordinator::create(config);
    
    SECTION("REALTIME borrows from HIGH") {
        std::vector<AudioBufferHandle> handles;
        // Acquire all 5 REALTIME buffers
        for (int i = 0; i < 5; ++i) {
            handles.push_back(coordinator->acquireBuffer(2, 256, IMemoryCoordinator::MemoryPriority::REALTIME));
        }
        
        // 6th REALTIME should borrow from HIGH (3), NORMAL (1), or BACKGROUND (1)
        auto handleBorrowed = coordinator->acquireBuffer(2, 256, IMemoryCoordinator::MemoryPriority::REALTIME);
        CHECK(handleBorrowed.isValid());
        
        // Count how many we can get total
        for (int i = 0; i < 4; ++i) {
            handles.push_back(coordinator->acquireBuffer(2, 256, IMemoryCoordinator::MemoryPriority::REALTIME));
            CHECK(handles.back().isValid());
        }
        
        // 11th should fail
        auto handleFail = coordinator->acquireBuffer(2, 256, IMemoryCoordinator::MemoryPriority::REALTIME);
        CHECK(!handleFail.isValid());
    }

    SECTION("NORMAL cannot borrow from REALTIME") {
        std::vector<AudioBufferHandle> handles;
        // Acquire 1 NORMAL buffer
        handles.push_back(coordinator->acquireBuffer(2, 256, IMemoryCoordinator::MemoryPriority::NORMAL));
        CHECK(handles.back().isValid());
        
        // 2nd NORMAL should borrow from BACKGROUND (1)
        handles.push_back(coordinator->acquireBuffer(2, 256, IMemoryCoordinator::MemoryPriority::NORMAL));
        CHECK(handles.back().isValid());

        // 3rd NORMAL should fail because it can't borrow from HIGH or REALTIME
        auto handleFail = coordinator->acquireBuffer(2, 256, IMemoryCoordinator::MemoryPriority::NORMAL);
        CHECK(!handleFail.isValid());
    }
}

TEST_CASE("Memory Stats Accuracy", "[Layer2][Memory]") {
    IMemoryCoordinator::PoolConfig config;
    config.numBuffers = 10;
    config.initialChannels = 2;
    config.initialFramesPerBuffer = 256;
    config.alignment = 64;
    config.realtimeBufferRatio = 100; // All REALTIME

    auto coordinator = IMemoryCoordinator::create(config);
    
    IMemoryCoordinator::RTStats stats;
    coordinator->getStats_RT(stats);
    CHECK(stats.totalBuffers == 10);
    CHECK(stats.availableBuffers == 10);
    
    {
        auto handle = coordinator->acquireBuffer(2, 256, IMemoryCoordinator::MemoryPriority::REALTIME);
        coordinator->getStats_RT(stats);
        CHECK(stats.availableBuffers == 9);
    }
    
    coordinator->getStats_RT(stats);
    CHECK(stats.availableBuffers == 10);
}
