#include <catch2/catch_test_macros.hpp>
#include "Core audio engine/scheduler/idsp_kernel.h"
#include "Core infrastructure/bridges/ievent_queue.h"
#include "Core infrastructure/bridges/imutation_bridge.h"
#include <vector>
#include <map>
#include <thread>
#include <chrono>
#include <iostream>

using namespace Layer3;
using namespace Layer2;

// RT-safe event storage
constexpr uint32_t MAX_TEST_EVENTS = 128;
struct NodeEventStorage {
    EventData events[MAX_TEST_EVENTS];
    std::atomic<uint32_t> count{0};
};
static NodeEventStorage g_nodeEvents[1024]; // Simple array for test IDs

void mockProcessFunc(NodeID nodeId, float* const* /*inputs*/, float* const* /*outputs*/,
                    uint32_t /*numChannels*/, uint32_t /*numSamples*/,
                    const EventData* events, uint32_t numEvents,
                    EventData* /*outEvents*/, uint32_t* /*outEventCount*/,
                    const ProcessContext* /*context*/, const bool* /*inputSilence*/,
                    bool* /*isOutputSilent*/) {
    uint32_t id = nodeId.id % 1024;
    uint32_t currentCount = g_nodeEvents[id].count.load(std::memory_order_relaxed);
    
    for (uint32_t i = 0; i < numEvents && currentCount < MAX_TEST_EVENTS; ++i) {
        g_nodeEvents[id].events[currentCount++] = events[i];
    }
    g_nodeEvents[id].count.store(currentCount, std::memory_order_release);
}

TEST_CASE("Scheduler: Parameter Mapping", "[Layer3][Scheduler]") {
    auto kernel = IDSPKernel::create(256);
    auto eventQueue = IEventQueue::create(IEventQueue::Config::defaultConfig());
    auto mutationBridge = IMutationBridge::create();
    
    kernel->attachEventQueue(eventQueue.get());
    kernel->attachMutationBridge(mutationBridge.get());
    kernel->registerProcessor(1, mockProcessFunc);
    
    for(auto& storage : g_nodeEvents) storage.count.store(0);
    
    float bufL[2048], bufR[2048];
    float* inputs[2] = {bufL, bufR};
    float* outputs[2] = {bufL, bufR};
    std::memset(bufL, 0, sizeof(bufL));
    std::memset(bufR, 0, sizeof(bufR));

    // Add nodes A and B
    NodeID idA = {100, 1};
    NodeID idB = {200, 1};

    SystemMutation mutA = {};
    mutA.type = MutationType::NODE_ADD;
    mutA.node.type = 1;
    mutA.node.id = idA;
    mutationBridge->pushMutation(mutA);
    
    SystemMutation mutB = {};
    mutB.type = MutationType::NODE_ADD;
    mutB.node.type = 1;
    mutB.node.id = idB;
    mutationBridge->pushMutation(mutB);
    
    SystemMutation mutConn = {};
    mutConn.type = MutationType::NODE_CONNECT;
    mutConn.connection.sourceNodeIndex = 0; // A
    mutConn.connection.destNodeIndex = 1;   // B
    mutConn.connection.gain = 1.0f;
    mutationBridge->pushMutation(mutConn);
    
    int retry = 0;
    while ((kernel->getNodeCount() < 2 || kernel->getConnectionCount() < 1) && retry < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ProcessContext ctx = {};
        kernel->process(inputs, outputs, 2, 512, &ctx);
        retry++;
    }
    
    REQUIRE(kernel->getNodeCount() == 2);
    
    // 1. Standard test
    {
        for(auto& storage : g_nodeEvents) storage.count.store(0);
        EventData event = EventHelpers::makeAutomationEvent(idA, 10, 0.5f, 100);
        eventQueue->pushEvent(event);
        eventQueue->prepareCycle();
        ProcessContext ctx = {};
        kernel->process(inputs, outputs, 2, 512, &ctx);
        
        REQUIRE(g_nodeEvents[idA.id % 1024].count.load() == 1);
        CHECK(g_nodeEvents[idA.id % 1024].events[0].sampleOffset == 100);
    }
    
    // 2. PDC test
    {
        auto kernelPDC = IDSPKernel::create(256);
        auto eventQueuePDC = Layer2::IEventQueue::create();
        kernelPDC->attachEventQueue(eventQueuePDC.get());
        kernelPDC->registerProcessor(1, mockProcessFunc);

        NodeID idLatent = {301, 1};
        NodeID idDry = {302, 1};
        
        // Setup graph: Latent -> Dry
        auto mutBridge = Layer2::IMutationBridge::create();
        kernelPDC->attachMutationBridge(mutBridge.get());
        
        SystemMutation m1 = {}; m1.type = MutationType::NODE_ADD; m1.node.id = idLatent; m1.node.type = 1;
        mutBridge->pushMutation(m1);
        SystemMutation m2 = {}; m2.type = MutationType::NODE_ADD; m2.node.id = idDry; m2.node.type = 1;
        mutBridge->pushMutation(m2);
        SystemMutation m3 = {}; m3.type = MutationType::NODE_CONNECT; m3.connection.sourceNodeIndex = 0; m3.connection.destNodeIndex = 1; m3.connection.gain = 1.0f;
        mutBridge->pushMutation(m3);

        for (int i = 0; i < 100; ++i) {
            ProcessContext ctx;
            ctx.currentBlockSize = 512;
            ctx.sampleRate = 48000.0f;
            kernelPDC->process(inputs, outputs, 2, 512, &ctx);
            if (kernelPDC->getNodeCount() == 2) break;
            std::this_thread::yield();
        }

        kernelPDC->setNodeLatency(idLatent, 512);
        uint32_t versionBeforePDC = kernelPDC->getTopologyVersion();
        kernelPDC->applyPDC();
        
        REQUIRE(kernelPDC->getNodeLatency(idLatent) == 512);

        // Wait for PDC update to be swapped in
        for (int i = 0; i < 100; ++i) {
            ProcessContext ctx; ctx.currentBlockSize = 1024; ctx.sampleRate = 48000.0f;
            kernelPDC->process(inputs, outputs, 2, 1024, &ctx);
            if (kernelPDC->getTopologyVersion() > versionBeforePDC) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        REQUIRE(kernelPDC->getTotalLatency() == 512);
        
        for(auto& storage : g_nodeEvents) storage.count.store(0);
        
        // Push event for latent node at offset 100
        // Shift = -cumul = -512. Result = 100 - 512 = -412.
        EventData ev1 = EventHelpers::makeAutomationEvent(idLatent, 10, 0.75f, 100);
        eventQueuePDC->pushEvent(ev1);
        
        ProcessContext ctx;
        ctx.currentBlockSize = 1024;
        ctx.sampleRate = 48000.0f;
        kernelPDC->process(inputs, outputs, 2, 1024, &ctx);
        
        // Should be dropped (clamped) from current cycle
        CHECK(g_nodeEvents[idLatent.id % 1024].count.load() == 0);
    }
}

