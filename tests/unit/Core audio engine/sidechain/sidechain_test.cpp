#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Core audio engine/sidechain/isidechain_manager.h"
#include "Core audio engine/scheduler/idsp_kernel.h"
#include "Core infrastructure/bridges/imutation_bridge.h"
#include <cmath>
#include <vector>
#include <thread>

using namespace Layer3;
using namespace Catch;

struct SidechainTestState {
    static ISidechainManager* manager;
    static float lastScValue;
};

ISidechainManager* SidechainTestState::manager = nullptr;
float SidechainTestState::lastScValue = 0.0f;

TEST_CASE("Sidechain Manager: Routing and Buffer Access", "[Layer3][Sidechain]") {
    auto manager = ISidechainManager::create();
    REQUIRE(manager != nullptr);

    NodeID targetNode = {10, 1};
    uint32_t inputIndex = 1;
    const float PI = 3.14159265359f;

    SECTION("Registration and Basic Access") {
        // Test that an unregistered sidechain returns nullptr
        REQUIRE(manager->getSidechainBuffer(targetNode, inputIndex) == nullptr);

        // Register sidechain input
        manager->registerSidechainInput(targetNode, inputIndex);

        // Get sidechain buffer
        float* buffer = manager->getSidechainBuffer(targetNode, inputIndex);
        REQUIRE(buffer != nullptr);

        // Fill buffer with test data (sine wave)
        const uint32_t numSamples = 512;
        for (uint32_t i = 0; i < numSamples; ++i) {
            buffer[i] = std::sin(2.0f * PI * 440.0f * i / 48000.0f);
        }

        // Verify data remains consistent
        float* bufferRead = manager->getSidechainBuffer(targetNode, inputIndex);
        REQUIRE(bufferRead == buffer);
        REQUIRE(bufferRead[100] == Approx(std::sin(2.0f * PI * 440.0f * 100 / 48000.0f)));
    }

    SECTION("Unregistration") {
        manager->registerSidechainInput(targetNode, inputIndex);
        REQUIRE(manager->getSidechainBuffer(targetNode, inputIndex) != nullptr);

        manager->unregisterSidechainInput(targetNode, inputIndex);
        REQUIRE(manager->getSidechainBuffer(targetNode, inputIndex) == nullptr);
    }

    SECTION("Multiple Sidechains") {
        NodeID nodeA = {1, 1};
        NodeID nodeB = {2, 1};
        
        manager->registerSidechainInput(nodeA, 0);
        manager->registerSidechainInput(nodeB, 0);
        
        float* bufA = manager->getSidechainBuffer(nodeA, 0);
        float* bufB = manager->getSidechainBuffer(nodeB, 0);
        
        REQUIRE(bufA != nullptr);
        REQUIRE(bufB != nullptr);
        REQUIRE(bufA != bufB);
        
        bufA[0] = 1.0f;
        bufB[0] = 2.0f;
        
        REQUIRE(manager->getSidechainBuffer(nodeA, 0)[0] == 1.0f);
        REQUIRE(manager->getSidechainBuffer(nodeB, 0)[0] == 2.0f);
    }

    SECTION("Invalid Access") {
        NodeID invalidNode = NodeID::invalid();
        REQUIRE(manager->getSidechainBuffer(invalidNode, 0) == nullptr);
        
        NodeID unknownNode = {99, 1};
        REQUIRE(manager->getSidechainBuffer(unknownNode, 0) == nullptr);
    }

    SECTION("Planar Buffer and Accumulation") {
        NodeID planarTargetNode = {12, 1};
        uint32_t inputIdx = 0;
        manager->registerSidechainInput(planarTargetNode, inputIdx);

        PlanarSidechainBuffer planarBuf = manager->getSidechainPlanarBuffer(planarTargetNode, inputIdx);
        REQUIRE(planarBuf.numChannels == 2);
        REQUIRE(planarBuf.channels[0] != nullptr);
        REQUIRE(planarBuf.channels[1] != nullptr);

        // Test mono input accumulation (mono to stereo sidechain expansion)
        float inData[256];
        for (int i = 0; i < 256; ++i) inData[i] = 0.75f;
        float* inputs[1] = { inData };

        manager->accumulateSidechainInput(planarTargetNode, inputIdx, inputs, 1, 256, 1.0f);

        REQUIRE(planarBuf.channels[0][0] == Approx(0.75f));
        REQUIRE(planarBuf.channels[1][0] == Approx(0.75f)); // Mono mirrored to channel 1

        // Test clearing buffers
        manager->clearAllBuffers(256);
        REQUIRE(planarBuf.channels[0][0] == 0.0f);
        REQUIRE(planarBuf.channels[1][0] == 0.0f);
    }

    SECTION("DSP Kernel Integration") {
        auto kernel = IDSPKernel::create(256);
        auto mutationBridge = Layer2::IMutationBridge::create();
        kernel->attachMutationBridge(mutationBridge.get());
        
        NodeID idA = {1, 1};
        NodeID idB = {2, 1};
        
        SidechainTestState::manager = manager.get();
        SidechainTestState::lastScValue = 0.0f;

        auto procAFunc = [](NodeID, float* const*, float* const*, uint32_t, uint32_t numSamples, const EventData*, uint32_t, EventData*, uint32_t*, const ProcessContext*, const bool*, bool*) {
            float* scBuf = SidechainTestState::manager->getSidechainBuffer({2, 1}, 0);
            if (scBuf) {
                for (uint32_t i = 0; i < numSamples; ++i) scBuf[i] = 0.5f;
            }
        };
        
        auto procBFunc = [](NodeID, float* const*, float* const*, uint32_t, uint32_t numSamples, const EventData*, uint32_t, EventData*, uint32_t*, const ProcessContext*, const bool*, bool*) {
            (void)numSamples;
            float* scBuf = SidechainTestState::manager->getSidechainBuffer({2, 1}, 0);
            if (scBuf) SidechainTestState::lastScValue = scBuf[0];
        };
        
        kernel->registerProcessor(1, procAFunc);
        kernel->registerProcessor(2, procBFunc);
        
        manager->registerSidechainInput(idB, 0);
        
        // Add nodes
        SystemMutation mutA = {}; 
        mutA.type = Layer2::MutationType::NODE_ADD; 
        mutA.node.id = idA; 
        mutA.node.type = 1;
        mutationBridge->pushMutation(mutA);
        
        SystemMutation mutB = {}; 
        mutB.type = Layer2::MutationType::NODE_ADD; 
        mutB.node.id = idB; 
        mutB.node.type = 2;
        mutationBridge->pushMutation(mutB);
        
        // Wait for swap
        for (int i = 0; i < 100; ++i) {
            ProcessContext ctx = {};
            kernel->process(nullptr, nullptr, 2, 512, &ctx);
            if (kernel->getNodeCount() == 2) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        ProcessContext ctx = {};
        kernel->process(nullptr, nullptr, 2, 512, &ctx);
        
        CHECK(SidechainTestState::lastScValue == 0.5f);
    }
}
