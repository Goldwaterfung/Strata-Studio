#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Middle Bridge/telemetry/metering_provider.h"
#include "Core infrastructure/bridges/itelemetry_bridge.h"
#include <memory>
#include <cmath>

TEST_CASE("MeteringProvider: Default Levels", "[MiddleBridge][Metering]") {
    auto telemetryBridge = Layer2::ITelemetryBridge::create(128);
    REQUIRE(telemetryBridge != nullptr);

    bridge::MeteringProvider provider(telemetryBridge.get());
    TrackID trackId{101, 1};

    // Unregistered track should return default quiet values
    auto levels = provider.getTrackLevels(trackId);
    CHECK(levels.peakLeft == -120.0f);
    CHECK(levels.peakRight == -120.0f);
    CHECK(levels.rmsLeft == -120.0f);
    CHECK(levels.rmsRight == -120.0f);
    CHECK_FALSE(levels.clipLeft);
    CHECK_FALSE(levels.clipRight);
}

TEST_CASE("MeteringProvider: Registration & Routing", "[MiddleBridge][Metering]") {
    auto telemetryBridge = Layer2::ITelemetryBridge::create(128);
    bridge::MeteringProvider provider(telemetryBridge.get());

    TrackID trackId{42, 1};
    NodeID nodeId{7, 1};

    // Register mapping
    provider.registerTrackNodeMapping(trackId, nodeId);

    // Initial state after mapping should still be silence
    auto levels = provider.getTrackLevels(trackId);
    CHECK(levels.peakLeft == -120.0f);

    // Unregister mapping
    provider.unregisterTrackNodeMapping(trackId);
    levels = provider.getTrackLevels(trackId);
    CHECK(levels.peakLeft == -120.0f);
}

TEST_CASE("MeteringProvider: Ballistics and Telemetry Processing", "[MiddleBridge][Metering]") {
    auto telemetryBridge = Layer2::ITelemetryBridge::create(128);
    bridge::MeteringProvider provider(telemetryBridge.get());

    TrackID trackId{1, 1};
    NodeID nodeId{10, 1};

    provider.registerTrackNodeMapping(trackId, nodeId);

    SECTION("Attack Ballistics (Rising Levels)") {
        // Push raw Peak frame for Left Channel (0) with linear amplitude 1.0 (0 dB)
        float peakLinearLeft = 1.0f;
        auto peakLeftFrame = Layer2::TelemetryHelpers::makePeakMeter(nodeId, peakLinearLeft, false);
        peakLeftFrame.payload[2] = 0; // Channel 0 (Left)
        telemetryBridge->pushTelemetry(peakLeftFrame);

        // Push raw Peak frame for Right Channel (1) with linear amplitude 0.5 (~ -6 dB)
        float peakLinearRight = 0.5f;
        auto peakRightFrame = Layer2::TelemetryHelpers::makePeakMeter(nodeId, peakLinearRight, false);
        peakRightFrame.payload[2] = 1; // Channel 1 (Right)
        telemetryBridge->pushTelemetry(peakRightFrame);

        // First update cycle: 10 milliseconds elapsed (matches peak attack constant)
        provider.updateMeters(10.0);

        auto levels = provider.getTrackLevels(trackId);
        
        // With attack coefficient calculated as 1 - exp(-10.0 / 10.0) ≈ 0.63212
        // Value rises from -120.0 dB towards 0 dB (Left) and -6.02 dB (Right)
        // Left should be around -120.0 + 0.63212 * 120.0 = -44.14 dB
        // Let's assert it rose significantly above -120 dB but did not fully reach target
        CHECK(levels.peakLeft > -50.0f);
        CHECK(levels.peakLeft < 0.0f);
        CHECK(levels.peakRight > -55.0f);
        CHECK(levels.peakRight < -6.0f);

        // Consecutive update cycles: let 200 ms pass
        provider.updateMeters(200.0);
        levels = provider.getTrackLevels(trackId);
        
        // Should have fully integrated / stabilized near targets
        CHECK(levels.peakLeft == Catch::Approx(0.0f).margin(0.1f));
        CHECK(levels.peakRight == Catch::Approx(-6.02f).margin(0.1f));
    }

    SECTION("Decay Ballistics (Falling Levels)") {
        // First raise fader to 0 dB
        auto frameL = Layer2::TelemetryHelpers::makePeakMeter(nodeId, 1.0f, false);
        frameL.payload[2] = 0;
        telemetryBridge->pushTelemetry(frameL);
        provider.updateMeters(500.0); // Fully charge filter

        CHECK(provider.getTrackLevels(trackId).peakLeft == Catch::Approx(0.0f).margin(0.01f));

        // Now push silence (0.0 linear / -120.0 dB)
        auto quietFrame = Layer2::TelemetryHelpers::makePeakMeter(nodeId, 0.0f, false);
        quietFrame.payload[2] = 0;
        telemetryBridge->pushTelemetry(quietFrame);

        // Decay time is 300 ms. Update by 300 ms.
        provider.updateMeters(300.0);
        
        auto levels = provider.getTrackLevels(trackId);
        // Peak decay coefficient: 1 - exp(-300 / 300) = 1 - exp(-1) ≈ 0.632
        // Remaining distance: 0.368 * -120 dB ≈ -75.8 dB
        CHECK(levels.peakLeft == Catch::Approx(-75.8f).margin(1.0f));
    }
}

TEST_CASE("MeteringProvider: Sticky Clip Logic", "[MiddleBridge][Metering]") {
    auto telemetryBridge = Layer2::ITelemetryBridge::create(128);
    bridge::MeteringProvider provider(telemetryBridge.get());

    TrackID trackId{5, 1};
    NodeID nodeId{12, 1};

    provider.registerTrackNodeMapping(trackId, nodeId);

    // Push clipped frame
    auto clipFrame = Layer2::TelemetryHelpers::makePeakMeter(nodeId, 1.1f, true);
    clipFrame.payload[2] = 0; // Left channel
    telemetryBridge->pushTelemetry(clipFrame);

    provider.updateMeters(10.0);
    
    auto levels = provider.getTrackLevels(trackId);
    CHECK(levels.clipLeft);
    CHECK_FALSE(levels.clipRight);

    // Next frames are quiet, but clip should remain sticky (true)
    auto quietFrame = Layer2::TelemetryHelpers::makePeakMeter(nodeId, 0.1f, false);
    quietFrame.payload[2] = 0;
    telemetryBridge->pushTelemetry(quietFrame);
    
    provider.updateMeters(10.0);
    levels = provider.getTrackLevels(trackId);
    CHECK(levels.clipLeft); // Still true!

    // Reset clip and verify it returns to false
    provider.resetTrackClip(trackId);
    levels = provider.getTrackLevels(trackId);
    CHECK_FALSE(levels.clipLeft);
}

TEST_CASE("MeteringProvider: Master Metering", "[MiddleBridge][Metering]") {
    auto telemetryBridge = Layer2::ITelemetryBridge::create(128);
    bridge::MeteringProvider provider(telemetryBridge.get());

    // Master telemetry frames are sent with an invalid source NodeID
    NodeID masterNodeId = NodeID::invalid();
    REQUIRE_FALSE(masterNodeId.isValid());

    auto masterFrame = Layer2::TelemetryHelpers::makePeakMeter(masterNodeId, 1.0f, true);
    masterFrame.payload[2] = 1; // Right channel
    telemetryBridge->pushTelemetry(masterFrame);

    provider.updateMeters(300.0);
    
    auto levels = provider.getMasterLevels();
    CHECK(levels.peakRight == Catch::Approx(0.0f).margin(0.1f));
    CHECK(levels.clipRight);

    provider.resetMasterClip();
    levels = provider.getMasterLevels();
    CHECK_FALSE(levels.clipRight);
}

TEST_CASE("MeteringProvider: Spectrum Simulation", "[MiddleBridge][Metering]") {
    auto telemetryBridge = Layer2::ITelemetryBridge::create(128);
    bridge::MeteringProvider provider(telemetryBridge.get());

    NodeID invalidNode{};
    float spectrum[64];
    
    // Invalid Node should return quiet values
    provider.getSpectrumData(invalidNode, spectrum, 64);
    for (int i = 0; i < 64; ++i) {
        CHECK(spectrum[i] == -120.0f);
    }

    NodeID validNode{4, 1};
    provider.getSpectrumData(validNode, spectrum, 64);
    
    // Verify spectrum has valid slope values (not default silence)
    for (int i = 0; i < 64; ++i) {
        CHECK(spectrum[i] > -120.0f);
        CHECK(spectrum[i] <= 0.0f);
    }
}
