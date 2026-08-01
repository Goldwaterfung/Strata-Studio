#include <catch2/catch_test_macros.hpp>
#include "Media management/export/iexport_service.h"
#include "Media management/registry/imedia_registry.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "Core audio engine/scheduler/idsp_kernel.h"
#include <thread>
#include <chrono>
#include <vector>
#include <fstream>

using namespace MediaManagement;

// Minimal DSP node that just fills buffer with a constant value
static void constantProcessor(NodeID nodeId, float* const* inputs, float* const* outputs,
                             uint32_t numChannels, uint32_t numSamples,
                             const EventData* events, uint32_t numEvents,
                             EventData* outEvents, uint32_t* outEventCount,
                             const ProcessContext* context) {
    (void)nodeId; (void)inputs; (void)events; (void)numEvents; (void)outEvents; (void)outEventCount; (void)context;
    float value = (nodeId.id == 1) ? 1.0f : 0.5f; // Node 1 = 1.0, others = 0.5
    for (uint32_t c = 0; c < numChannels; ++c) {
        for (uint32_t s = 0; s < numSamples; ++s) {
            outputs[c][s] = value;
        }
    }
}

TEST_CASE("Export Service Stem Isolation Reproduction", "[MediaManagement][Export][Repro]") {
    auto registry = IMediaRegistry::create();
    auto strings = Layer2::IStringRegistry::create();
    auto kernel = Layer3::IDSPKernel::create();
    
    // Register processor
    kernel->registerProcessor(0, constantProcessor);
    
    // Setup a simple topology: Node 1 (1.0) and Node 2 (0.5)
    // In a real scenario, they would both connect to a Master node (Node 3)
    // But our current kernel just returns the last node's output.
    
    // We'll simulate a stem export for Node 1.
    // If isolation is NOT working, it will likely return the last node (Node 2) or a mix.
    
    // Note: To actually get the nodes into the kernel, we'd normally use a MutationBridge.
    // For this test, we'll manually trigger mutations if possible, or just assume 
    // the kernel implementation we saw is what we're testing against.
    
    // Since we can't easily push mutations without a bridge in this unit test,
    // let's just look at the ExportServiceImpl code again.
    
    // Actually, I can't run this test and verify the audio content easily because 
    // I don't have a WAV reader in the test environment yet (only writer).
    // But I can check the code and see the bug is definitely there.
}
