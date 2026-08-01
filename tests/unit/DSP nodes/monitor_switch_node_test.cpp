#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "DSP nodes/monitor_switch/monitor_switch_node.h"
#include <vector>
#include <cstring>

TEST_CASE("MonitorSwitchNode: Routing and Summing Modes", "[DSP][MonitorSwitch]") {
    DSP::MonitorSwitchFactory factory;
    auto node = factory.createNode();
    REQUIRE(node.isValid());

    auto* state = factory.getRegistry().get(node);
    REQUIRE(state != nullptr);
    CHECK(state->monitorState == 0); // Default is OFF (0)

    const uint32_t numSamples = 64;
    const uint32_t numChannels = 2;

    std::vector<float> physL(numSamples, 1.5f);
    std::vector<float> physR(numSamples, 2.5f);
    std::vector<float> playL(numSamples, 10.0f);
    std::vector<float> playR(numSamples, 20.0f);

    float* inputs[4] = { physL.data(), physR.data(), playL.data(), playR.data() };

    std::vector<float> outL(numSamples, 0.0f);
    std::vector<float> outR(numSamples, 0.0f);
    float* outputs[2] = { outL.data(), outR.data() };

    ProcessContext context{};
    context.transportState = TransportState::STOPPED;

    EventData outEvents[16];
    uint32_t outEventCount = 0;

    SECTION("MonitorState::OFF - Playback Only") {
        // Explicitly set state to OFF (0)
        state->monitorState = 0;

        bool inputSilence[4] = { false, false, false, false };
        bool isOutputSilent = false;

        DSP::processMonitorSwitch(
            node, inputs, outputs, numChannels, numSamples,
            nullptr, 0, outEvents, &outEventCount,
            &context, inputSilence, &isOutputSilent
        );

        CHECK_FALSE(isOutputSilent);
        for (uint32_t i = 0; i < numSamples; ++i) {
            CHECK(outputs[0][i] == Catch::Approx(10.0f));
            CHECK(outputs[1][i] == Catch::Approx(20.0f));
        }

        // Test playback silent
        inputSilence[2] = true; // Playback Left silent
        inputSilence[3] = true;
        DSP::processMonitorSwitch(
            node, inputs, outputs, numChannels, numSamples,
            nullptr, 0, outEvents, &outEventCount,
            &context, inputSilence, &isOutputSilent
        );
        CHECK(isOutputSilent);
        for (uint32_t i = 0; i < numSamples; ++i) {
            CHECK(outputs[0][i] == 0.0f);
            CHECK(outputs[1][i] == 0.0f);
        }
    }

    SECTION("MonitorState::ON - Input Only") {
        // Set state to ON (1)
        state->monitorState = 1;

        bool inputSilence[4] = { false, false, false, false };
        bool isOutputSilent = false;

        DSP::processMonitorSwitch(
            node, inputs, outputs, numChannels, numSamples,
            nullptr, 0, outEvents, &outEventCount,
            &context, inputSilence, &isOutputSilent
        );

        CHECK_FALSE(isOutputSilent);
        for (uint32_t i = 0; i < numSamples; ++i) {
            CHECK(outputs[0][i] == Catch::Approx(1.5f));
            CHECK(outputs[1][i] == Catch::Approx(2.5f));
        }

        // Test input silent
        inputSilence[0] = true; // Physical Left silent
        inputSilence[1] = true;
        DSP::processMonitorSwitch(
            node, inputs, outputs, numChannels, numSamples,
            nullptr, 0, outEvents, &outEventCount,
            &context, inputSilence, &isOutputSilent
        );
        CHECK(isOutputSilent);
        for (uint32_t i = 0; i < numSamples; ++i) {
            CHECK(outputs[0][i] == 0.0f);
            CHECK(outputs[1][i] == 0.0f);
        }
    }

    SECTION("MonitorState::AUTO - Summing Mode (Stopped)") {
        // Set state to AUTO (2)
        state->monitorState = 2;
        context.transportState = TransportState::STOPPED;

        bool inputSilence[4] = { false, false, false, false };
        bool isOutputSilent = false;

        // Both active
        DSP::processMonitorSwitch(
            node, inputs, outputs, numChannels, numSamples,
            nullptr, 0, outEvents, &outEventCount,
            &context, inputSilence, &isOutputSilent
        );

        CHECK_FALSE(isOutputSilent);
        for (uint32_t i = 0; i < numSamples; ++i) {
            CHECK(outputs[0][i] == Catch::Approx(11.5f)); // 1.5 + 10.0
            CHECK(outputs[1][i] == Catch::Approx(22.5f)); // 2.5 + 20.0
        }

        // Physical silent -> copies Playback
        inputSilence[0] = true;
        inputSilence[1] = true;
        DSP::processMonitorSwitch(
            node, inputs, outputs, numChannels, numSamples,
            nullptr, 0, outEvents, &outEventCount,
            &context, inputSilence, &isOutputSilent
        );
        CHECK_FALSE(isOutputSilent);
        for (uint32_t i = 0; i < numSamples; ++i) {
            CHECK(outputs[0][i] == Catch::Approx(10.0f));
            CHECK(outputs[1][i] == Catch::Approx(20.0f));
        }

        // Playback silent (but physical active) -> copies Physical
        inputSilence[0] = false;
        inputSilence[1] = false;
        inputSilence[2] = true;
        inputSilence[3] = true;
        DSP::processMonitorSwitch(
            node, inputs, outputs, numChannels, numSamples,
            nullptr, 0, outEvents, &outEventCount,
            &context, inputSilence, &isOutputSilent
        );
        CHECK_FALSE(isOutputSilent);
        for (uint32_t i = 0; i < numSamples; ++i) {
            CHECK(outputs[0][i] == Catch::Approx(1.5f));
            CHECK(outputs[1][i] == Catch::Approx(2.5f));
        }

        // Both silent -> output silent
        inputSilence[0] = true;
        inputSilence[1] = true;
        inputSilence[2] = true;
        inputSilence[3] = true;
        DSP::processMonitorSwitch(
            node, inputs, outputs, numChannels, numSamples,
            nullptr, 0, outEvents, &outEventCount,
            &context, inputSilence, &isOutputSilent
        );
        CHECK(isOutputSilent);
        for (uint32_t i = 0; i < numSamples; ++i) {
            CHECK(outputs[0][i] == 0.0f);
            CHECK(outputs[1][i] == 0.0f);
        }
    }

    SECTION("MonitorState::AUTO - Input Only (Recording)") {
        // Set state to AUTO (2)
        state->monitorState = 2;
        context.transportState = TransportState::RECORDING;

        bool inputSilence[4] = { false, false, false, false };
        bool isOutputSilent = false;

        DSP::processMonitorSwitch(
            node, inputs, outputs, numChannels, numSamples,
            nullptr, 0, outEvents, &outEventCount,
            &context, inputSilence, &isOutputSilent
        );

        CHECK_FALSE(isOutputSilent);
        for (uint32_t i = 0; i < numSamples; ++i) {
            CHECK(outputs[0][i] == Catch::Approx(1.5f));
            CHECK(outputs[1][i] == Catch::Approx(2.5f));
        }

        // Test input silent
        inputSilence[0] = true;
        inputSilence[1] = true;
        DSP::processMonitorSwitch(
            node, inputs, outputs, numChannels, numSamples,
            nullptr, 0, outEvents, &outEventCount,
            &context, inputSilence, &isOutputSilent
        );
        CHECK(isOutputSilent);
        for (uint32_t i = 0; i < numSamples; ++i) {
            CHECK(outputs[0][i] == 0.0f);
            CHECK(outputs[1][i] == 0.0f);
        }
    }

    SECTION("Automation events update monitorState") {
        EventData event;
        event.eventType = EventType::AUTOMATION;
        event.payload.automation.parameterIndex = 0;
        event.payload.automation.targetValue = 1.0f; // Switch to ON (1)

        bool inputSilence[4] = { false, false, false, false };
        bool isOutputSilent = false;

        DSP::processMonitorSwitch(
            node, inputs, outputs, numChannels, numSamples,
            &event, 1, outEvents, &outEventCount,
            &context, inputSilence, &isOutputSilent
        );

        CHECK(state->monitorState == 1);
        for (uint32_t i = 0; i < numSamples; ++i) {
            CHECK(outputs[0][i] == Catch::Approx(1.5f));
            CHECK(outputs[1][i] == Catch::Approx(2.5f));
        }
    }

    factory.destroyNode(node);
}
