#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "DSP nodes/latency/latency_node.h"
#include <vector>
#include <cstring>

TEST_CASE("LatencyNode Optimization Verification", "[DSP][LatencyNode]") {
    DSP::LatencyFactory factory;
    auto node = factory.createNode();
    REQUIRE(node.isValid());

    auto* state = factory.getRegistry().get(node);
    REQUIRE(state != nullptr);
    CHECK(state->delaySamples == 0);
    CHECK(state->writePos == 0);
    CHECK(state->bufferSize == 0);
    CHECK(state->silentSamplesProcessed == 0);

    const uint32_t numSamples = 64;
    const uint32_t numChannels = 2;

    std::vector<float> inL(numSamples, 0.0f);
    std::vector<float> inR(numSamples, 0.0f);
    float* inputs[2] = { inL.data(), inR.data() };

    std::vector<float> outL(numSamples, 999.0f); // Pre-fill with sentinel
    std::vector<float> outR(numSamples, 999.0f);
    float* outputs[2] = { outL.data(), outR.data() };

    EventData outEvents[16];
    uint32_t outEventCount = 0;
    ProcessContext context{};

    SECTION("Power-of-Two Buffer Sizing") {
        factory.setLatency(node, 100); // 100 + 2048 = 2148
        // Next power of two of 2148 is 4096
        CHECK(state->bufferSize == 4096);

        factory.setLatency(node, 2100); // 2100 + 2048 = 4148
        // Next power of two of 4148 is 8192
        CHECK(state->bufferSize == 8192);
    }

    SECTION("Zero Latency Pass-Through") {
        factory.setLatency(node, 0);
        
        // Fill input with data
        std::fill(inL.begin(), inL.end(), 1.5f);
        std::fill(inR.begin(), inR.end(), 2.5f);

        bool inputSilence[2] = { false, false };
        bool isOutputSilent = false;

        DSP::processLatency(
            node, inputs, outputs, numChannels, numSamples,
            nullptr, 0, outEvents, &outEventCount,
            &context, inputSilence, &isOutputSilent
        );

        CHECK_FALSE(isOutputSilent);
        for (uint32_t i = 0; i < numSamples; ++i) {
            CHECK(outputs[0][i] == Catch::Approx(1.5f));
            CHECK(outputs[1][i] == Catch::Approx(2.5f));
        }
    }

    SECTION("Active Delay Processing (Impulse Response)") {
        const uint32_t delay = 4;
        factory.setLatency(node, delay);

        // Put an impulse at sample index 2
        inL[2] = 1.0f;
        inR[2] = -1.0f;

        bool inputSilence[2] = { false, false };
        bool isOutputSilent = false;

        DSP::processLatency(
            node, inputs, outputs, numChannels, numSamples,
            nullptr, 0, outEvents, &outEventCount,
            &context, inputSilence, &isOutputSilent
        );

        CHECK_FALSE(isOutputSilent);
        // Output index 2 + delay (6) should have the impulse
        CHECK(outputs[0][6] == Catch::Approx(1.0f));
        CHECK(outputs[1][6] == Catch::Approx(-1.0f));

        // Other samples should be 0 (since delay line buffer starts zeroed)
        CHECK(outputs[0][0] == Catch::Approx(0.0f));
        CHECK(outputs[0][5] == Catch::Approx(0.0f));
        CHECK(outputs[0][7] == Catch::Approx(0.0f));
    }

    SECTION("Silence Gating & Sleep Transition") {
        const uint32_t delay = 10;
        factory.setLatency(node, delay);

        // Set input silence flag to true
        bool inputSilence[2] = { true, true };
        bool isOutputSilent = false;

        // Block size is 64, which is > delay (10). It should sleep in the first block.
        DSP::processLatency(
            node, inputs, outputs, numChannels, numSamples,
            nullptr, 0, outEvents, &outEventCount,
            &context, inputSilence, &isOutputSilent
        );

        CHECK(isOutputSilent);
        CHECK(state->silentSamplesProcessed >= delay);

        for (uint32_t i = 0; i < numSamples; ++i) {
            CHECK(outputs[0][i] == 0.0f);
            CHECK(outputs[1][i] == 0.0f);
        }
    }

    SECTION("Ghost Burst Prevention on Sleep") {
        const uint32_t delay = 10;
        factory.setLatency(node, delay);

        // Write non-zero values into delay buffer to simulate previous audio
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            if (state->delayBuffers[ch]) {
                std::fill(state->delayBuffers[ch], state->delayBuffers[ch] + state->bufferSize, 5.0f);
            }
        }

        bool inputSilence[2] = { true, true };
        bool isOutputSilent = false;

        // Run block 1. Samples processed (64) >= delay (10), so it transitions to sleep
        DSP::processLatency(
            node, inputs, outputs, numChannels, numSamples,
            nullptr, 0, outEvents, &outEventCount,
            &context, inputSilence, &isOutputSilent
        );

        CHECK(isOutputSilent);

        // Verify that the delay buffers are cleared (to prevent ghost bursts when seeking later)
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            for (uint32_t i = 0; i < state->bufferSize; ++i) {
                CHECK(state->delayBuffers[ch][i] == 0.0f);
            }
        }
    }

    factory.destroyNode(node);
}
