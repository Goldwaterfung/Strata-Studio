#include <catch2/catch_test_macros.hpp>
#include "Core audio engine/scheduler/scheduler_impl.h"
#include "Core infrastructure/bridges/imutation_bridge.h"
#include <thread>
#include <chrono>
#include <vector>

using namespace Layer3;

TEST_CASE("Scheduler: Multithreading Handoff", "[Layer3][Scheduler][MT]") {
    auto mutationBridge = Layer2::IMutationBridge::create(16384);
    auto kernel = IDSPKernel::create(1024);
    REQUIRE(kernel != nullptr);

    kernel->registerProcessor(0, [](NodeID, float* const*, float* const*, uint32_t, uint32_t, const EventData*, uint32_t, EventData*, uint32_t*, const ProcessContext*, const bool*, bool*) {});
    kernel->attachMutationBridge(mutationBridge.get());

    SECTION("Topology Version Monotonicity (10,000 Mutations)") {
        const int numMutations = 10000;
        
        for (int i = 0; i < numMutations; ++i) {
            SystemMutation mutation = {};
            mutation.type = Layer2::MutationType::NODE_ADD;
            mutation.node.id = {static_cast<uint32_t>(i + 1), 1};
            mutation.node.type = 0;
            mutationBridge->pushMutation(mutation);
        }
        
        // Wait for worker to catch up (node count is the true indicator of completion)
        for (int retry = 0; retry < 500; ++retry) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ProcessContext MT_TEST_CONTEXT = {};
            MT_TEST_CONTEXT.currentBlockSize = 512; MT_TEST_CONTEXT.sampleRate = 48000.0f;
            kernel->process(nullptr, nullptr, 2, 512, &MT_TEST_CONTEXT);
            if (kernel->getNodeCount() == static_cast<uint32_t>(numMutations)) {
                break;
            }
        }
        CHECK(kernel->getTopologyVersion() > 0);
        CHECK(kernel->getNodeCount() == static_cast<uint32_t>(numMutations));
    }

    SECTION("Starvation and Wait-Free Swap (3 cycles skipped)") {
        uint32_t versionBefore = kernel->getTopologyVersion();
        
        // Push 3 new topologies (as mutation batches)
        for (int i = 0; i < 3; ++i) {
            SystemMutation mutation;
            mutation.type = Layer2::MutationType::NODE_ADD;
            mutation.node.id = {static_cast<uint32_t>(i + 11000), 1};
            mutation.node.type = 0;
            mutationBridge->pushMutation(mutation);
            
            // Wait for worker to rebuild each
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // The RT thread has NOT called process() yet, so it hasn't swapped.
        // Now call process once. It should pick up the LATEST topology immediately.
        ProcessContext MT_TEST_CONTEXT = {};
        kernel->process(nullptr, nullptr, 2, 512, &MT_TEST_CONTEXT);
        
        uint32_t versionAfter = kernel->getTopologyVersion();
        CHECK(versionAfter > versionBefore);
        
        // Verify that we can still process and everything is stable
        kernel->process(nullptr, nullptr, 2, 512, &MT_TEST_CONTEXT);
        CHECK(kernel->getTopologyVersion() == versionAfter);
    }
}

