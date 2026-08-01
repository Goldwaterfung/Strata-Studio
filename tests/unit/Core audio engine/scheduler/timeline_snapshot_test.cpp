// tests/unit/Core audio engine/scheduler/timeline_snapshot_test.cpp
#include <catch2/catch_test_macros.hpp>
#include "Core audio engine/scheduler/scheduler_impl.h"
#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>

using namespace Layer3;

TEST_CASE("TimelineSnapshot: Creation, Sorting, and Atomic Publication", "[CoreAudioEngine][TimelineSnapshot]") {
    // 1. Test POD compliance
    STATIC_CHECK(std::is_pod<SnapshotRegion>::value);
    STATIC_CHECK(std::is_pod<TimelineSnapshot>::value);

    // 2. Test Sorting logic
    TimelineSnapshot snapshot;
    snapshot.regionCount = 3;

    // Region 0: Pos 500
    snapshot.regions[0].trackId = {1, 1};
    snapshot.regions[0].type = RegionType::AUDIO;
    snapshot.regions[0].positionSample = 500;

    // Region 1: Pos 100
    snapshot.regions[1].trackId = {2, 1};
    snapshot.regions[1].type = RegionType::MIDI;
    snapshot.regions[1].positionSample = 100;

    // Region 2: Pos 1000
    snapshot.regions[2].trackId = {3, 1};
    snapshot.regions[2].type = RegionType::AUDIO;
    snapshot.regions[2].positionSample = 1000;

    // Sort ascending by positionSample
    std::sort(snapshot.regions, 
              snapshot.regions + snapshot.regionCount,
              [](const SnapshotRegion& a, const SnapshotRegion& b) {
                  return a.positionSample < b.positionSample;
              });

    REQUIRE(snapshot.regions[0].positionSample == 100);
    REQUIRE(snapshot.regions[1].positionSample == 500);
    REQUIRE(snapshot.regions[2].positionSample == 1000);

    // 3. Test IDSPKernel publication and acquisition
    std::unique_ptr<IDSPKernel> kernel = IDSPKernel::create(16);
    REQUIRE(kernel != nullptr);

    // Get active snapshot initial state (should be empty/all zeroed)
    const TimelineSnapshot* active = kernel->getActiveTimelineSnapshot();
    REQUIRE(active != nullptr);
    CHECK(active->regionCount == 0);

    // Publish snapshot
    kernel->publishTimelineSnapshot(snapshot);

    // Get active snapshot again
    active = kernel->getActiveTimelineSnapshot();
    REQUIRE(active != nullptr);
    CHECK(active->regionCount == 3);
    CHECK(active->regions[0].positionSample == 100);
    CHECK(active->regions[1].positionSample == 500);
    CHECK(active->regions[2].positionSample == 1000);
}

TEST_CASE("TimelineSnapshot: Thread-Safe Atomic swap", "[CoreAudioEngine][TimelineSnapshot]") {
    std::unique_ptr<IDSPKernel> kernel = IDSPKernel::create(16);
    REQUIRE(kernel != nullptr);

    std::atomic<bool> running{true};
    std::atomic<uint64_t> swapCount{0};

    // Thread writing snapshots
    std::thread writer([&]() {
        TimelineSnapshot snapshot;
        uint64_t count = 0;
        while (running.load(std::memory_order_relaxed)) {
            snapshot.regionCount = 1;
            snapshot.regions[0].positionSample = count++;
            kernel->publishTimelineSnapshot(snapshot);
            swapCount.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    });

    // Thread reading active snapshot (simulating audio callback)
    std::thread reader([&]() {
        uint64_t lastVal = 0;
        while (running.load(std::memory_order_relaxed)) {
            const TimelineSnapshot* active = kernel->getActiveTimelineSnapshot();
            if (active && active->regionCount > 0) {
                uint64_t currentVal = active->regions[0].positionSample;
                REQUIRE(currentVal >= lastVal); // Monotonically increasing
                lastVal = currentVal;
            }
            std::this_thread::yield();
        }
    });

    // Run for 100 milliseconds
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    running.store(false, std::memory_order_relaxed);

    writer.join();
    reader.join();

    CHECK(swapCount.load() > 0);
}
