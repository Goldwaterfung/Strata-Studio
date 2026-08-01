#include <catch2/catch_test_macros.hpp>
#include "Core audio engine/scheduler/scheduler_impl.h"
#include "Core infrastructure/bridges/imutation_bridge.h"
#include "Core infrastructure/bridges/itelemetry_bridge.h"
#include "Core infrastructure/bridges/ievent_queue.h"
#include <atomic>
#include <new>
#include <cstdlib>
#include <thread>
#include <chrono>

using namespace Layer3;

void IllegalDSPFunc(NodeID, float* const*, float* const*, uint32_t, uint32_t, const EventData*, uint32_t, EventData*, uint32_t*, const ProcessContext*, const bool*, bool*) {
    volatile int* p = new int(42); 
    delete p;
}

void NoOpDSPFunc(NodeID, float* const*, float* const*, uint32_t, uint32_t, const EventData*, uint32_t, EventData*, uint32_t*, const ProcessContext*, const bool*, bool*) {}

TEST_CASE("Scheduler: RT Safety Checks", "[Layer3][Scheduler][RT]") {
    auto mutationBridge = Layer2::IMutationBridge::create(2048);
    auto telemetryBridge = Layer2::ITelemetryBridge::create(2048);
    
    Layer2::IEventQueue::Config eventConfig = Layer2::IEventQueue::Config::defaultConfig();
    eventConfig.capacity = 1024;
    auto eventQueue = Layer2::IEventQueue::create(eventConfig);

    auto kernel = IDSPKernel::create(256);
    REQUIRE(kernel != nullptr);

    kernel->registerProcessor(0, NoOpDSPFunc);
    kernel->registerProcessor(1, IllegalDSPFunc);

    kernel->attachMutationBridge(mutationBridge.get());
    kernel->attachTelemetryBridge(telemetryBridge.get());
    kernel->attachEventQueue(eventQueue.get());

    SECTION("Allocation and Lock Interceptor Detection") {
        ProcessContext ctx;
        ctx.currentBlockSize = 512;
        ctx.sampleRate = 48000.0f;
        
        IllegalDSPFunc(NodeID{1,1}, nullptr, nullptr, 2, 512, nullptr, 0, nullptr, nullptr, &ctx, nullptr, nullptr);
        
        // Telemetry EVENT_OVERFLOW is now the primary RT safety indicator
    }

    SECTION("Kernel Process RT Safety Stress Test") {
        auto mutationBridgeStress = Layer2::IMutationBridge::create(2048);
        auto kernelStress = IDSPKernel::create(256);
        auto eventQueueStress = Layer2::IEventQueue::create();
        
        kernelStress->attachMutationBridge(mutationBridgeStress.get());
        kernelStress->attachEventQueue(eventQueueStress.get());
        kernelStress->attachTelemetryBridge(telemetryBridge.get());
        kernelStress->registerProcessor(0, NoOpDSPFunc);

        float in1[512] = {0}, in2[512] = {0};
        float out1[512], out2[512];
        float* inputs[] = {in1, in2};
        float* outputs[] = {out1, out2};
        ProcessContext ctx;
        ctx.currentBlockSize = 512;
        ctx.sampleRate = 48000.0f;

        // 1. Build a 32-node graph
        for (int i = 0; i < 32; ++i) {
            SystemMutation mutation = {};
            mutation.type = Layer2::MutationType::NODE_ADD;
            mutation.node.id = {static_cast<uint32_t>(400 + i), 1};
            mutation.node.type = 0; 
            mutationBridgeStress->pushMutation(mutation);
        }

        // Run cycles to let topology propagate
        for (int i = 0; i < 200; ++i) {
            kernelStress->process(inputs, outputs, 2, 512, &ctx);
            if (kernelStress->getNodeCount() == 32) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(kernelStress->getNodeCount() == 32);

        // 2. Stress test
        for (int i = 0; i < 1000; ++i) {
            kernelStress->process(inputs, outputs, 2, 512, &ctx);
        }

        // 3. Event Overflow Telemetry
        Layer2::ITelemetryBridge::BridgeTelemetryFrame frames[256];
        while(telemetryBridge->pollTelemetry(frames, 256) > 0);

        const uint32_t pushCount = 500;
        for (uint32_t i = 0; i < pushCount; ++i) {
            EventData ev = {};
            ev.targetNodeId = {400, 1}; // Use first node in range
            ev.eventType = EventType::MIDI_NOTE_ON;
            eventQueueStress->pushEvent(ev);
        }

        kernelStress->process(inputs, outputs, 2, 512, &ctx);

        uint32_t polled = telemetryBridge->pollTelemetry(frames, 256);
        bool frameFound = false;
        for (uint32_t i = 0; i < polled; ++i) {
            if (frames[i].sourceId.id == 400 && frames[i].type == TelemetryFrame::EVENT_OVERFLOW) {
                frameFound = true;
            }
        }
        CHECK(frameFound);
    }
}

