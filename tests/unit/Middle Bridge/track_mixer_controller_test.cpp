#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Middle Bridge/tracks/track_controller.h"
#include "mocks/mock_track_environment.h"
#include "DSP nodes/tracks/audio_track_node.h"

TEST_CASE("TrackMixerController Integration", "[bridge][mixer]") {
    auto stringRegistry = Layer2::IStringRegistry::create();
    auto mutationBridge = Layer2::IMutationBridge::create(128);
    MockTrackManager trackManager;
    trackManager.mutationBridge = mutationBridge.get();
    MockMeteringProvider meteringProvider;
    MockSessionManager sessionManager;
    auto session = std::make_unique<MockProjectSession>(&trackManager);
    sessionManager.setActiveSession(std::move(session));

    bridge::TrackController controller(
        &sessionManager,
        mutationBridge.get(),
        stringRegistry.get(),
        &meteringProvider
    );

    TrackID trackId = controller.addAudioTrack("Bass", 1, 0xFF555555);
    auto desc = trackManager.getPipelineDescriptor(trackId);

    SECTION("Fader Gain (Volume) Updates") {
        // Set volume to 0.5 linear
        controller.setFaderGain(trackId, 0.5f);

        // Check value in AudioTrack DSP registry slot
        auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode);
        REQUIRE(trk != nullptr);
        CHECK(trk->channelStrip.targetGain == 0.5f);

        // Check if mutation was pushed to mutation bridge
        SystemMutation mut{};
        CHECK(mutationBridge->popMutation(mut));
        CHECK(mut.type == 30); // PARAMETER_SET
        CHECK(mut.targetId == desc.trackNode);
        CHECK(mut.payload[0] == static_cast<uint32_t>(TrackMacroParameter::Volume));
        float val = 0.0f;
        std::memcpy(&val, &mut.payload[1], sizeof(float));
        CHECK(val == 0.5f);
    }

    SECTION("Pan Updates") {
        controller.setPan(trackId, 0.2f);

        auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode);
        REQUIRE(trk != nullptr);
        CHECK(trk->channelStrip.targetPan == 0.2f);

        SystemMutation mut{};
        CHECK(mutationBridge->popMutation(mut));
        CHECK(mut.type == 30); // PARAMETER_SET
        CHECK(mut.payload[0] == static_cast<uint32_t>(TrackMacroParameter::Pan));
    }

    SECTION("Mute Updates") {
        controller.setMute(trackId, true);

        auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode);
        REQUIRE(trk != nullptr);
        CHECK(trk->channelStrip.mute);

        SystemMutation mut{};
        CHECK(mutationBridge->popMutation(mut));
        CHECK(mut.type == 30); // PARAMETER_SET
        CHECK(mut.payload[0] == static_cast<uint32_t>(TrackMacroParameter::Mute));
    }

    SECTION("Solo Updates") {
        controller.setSolo(trackId, true);

        auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode);
        REQUIRE(trk != nullptr);
        CHECK(trk->channelStrip.solo);

        SystemMutation mut{};
        CHECK(mutationBridge->popMutation(mut));
        CHECK(mut.type == 30); // PARAMETER_SET
        CHECK(mut.payload[0] == static_cast<uint32_t>(TrackMacroParameter::Solo));
    }

    SECTION("Record Arming and Input Monitoring") {
        controller.setRecordArmed(trackId, true);
        auto state = controller.getTrackState(trackId);
        CHECK(state.isRecordArmed);

        controller.setInputMonitoring(trackId, true);
        state = controller.getTrackState(trackId);
        CHECK(state.isInputMonitoring);
    }

    SECTION("Track Mode Switching") {
        auto state = controller.getTrackState(trackId);
        CHECK(state.type == composition::TrackType::AUDIO);

        controller.setTrackMode(trackId, composition::TrackType::INSTRUMENT);
        state = controller.getTrackState(trackId);
        CHECK(state.type == composition::TrackType::INSTRUMENT);
    }
}

