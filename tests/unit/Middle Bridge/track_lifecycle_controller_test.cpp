#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Middle Bridge/tracks/track_controller.h"
#include "mocks/mock_track_environment.h"

TEST_CASE("TrackController: Lifecycle Management", "[MiddleBridge][TrackController]") {
    auto stringRegistry = Layer2::IStringRegistry::create();
    auto mutationBridge = Layer2::IMutationBridge::create(128);
    MockTrackManager trackManager;
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

    SECTION("Add Audio and Aux Tracks") {
        CHECK(controller.getTrackCount() == 0);

        TrackID track1 = controller.addAudioTrack("Vocal", 2, 0xFFFF0000);
        CHECK(track1.isValid());
        CHECK(controller.getTrackCount() == 1);

        TrackID track2 = controller.addAuxTrack("Reverb Aux", 0xFF00FF00);
        CHECK(track2.isValid());
        CHECK(controller.getTrackCount() == 2);

        // Verify registration in the metering provider mapped to channel strip
        auto desc1 = trackManager.getPipelineDescriptor(track1);
        auto desc2 = trackManager.getPipelineDescriptor(track2);
        
        CHECK(meteringProvider.mappings[track1.toRaw()] == desc1.trackNode);
        CHECK(meteringProvider.mappings[track2.toRaw()] == desc2.trackNode);

        // Remove track and check cleanup
        controller.removeTrack(track1);
        CHECK(controller.getTrackCount() == 1);
        CHECK(meteringProvider.mappings.find(track1.toRaw()) == meteringProvider.mappings.end());
    }

    SECTION("Rename, Color, and Move Tracks") {
        TrackID trackId = controller.addAudioTrack("Guitar", 2, 0xFF0000FF);
        for (int i = 0; i < 5; ++i) {
            controller.addAudioTrack("Dummy", 2, 0xFF0000FF);
        }
        
        // Rename
        controller.renameTrack(trackId, "Guitar Solo");
        auto state = controller.getTrackState(trackId);
        CHECK(std::string(state.name) == "Guitar Solo");

        // Color change
        controller.setTrackColor(trackId, 0xFFFFFF00);
        state = controller.getTrackState(trackId);
        CHECK(state.colorARGB == 0xFFFFFF00);

        // Move track
        controller.moveTrack(trackId, 5, TrackID::invalid());
        CHECK(trackManager.getTrackIndexPosition(trackId) == 4);
    }
}


TEST_CASE("TrackController: Unique Track Naming", "[MiddleBridge][TrackController][Naming]") {
    auto stringRegistry = Layer2::IStringRegistry::create();
    auto mutationBridge = Layer2::IMutationBridge::create(128);
    MockTrackManager trackManager;
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

    SECTION("Adding multiple default tracks resolves collisions") {
        TrackID t1 = controller.addAudioTrack("Audio", 2, 0xFF0000FF);
        TrackID t2 = controller.addAudioTrack("Audio", 2, 0xFF0000FF);
        TrackID t3 = controller.addAudioTrack("Audio", 2, 0xFF0000FF);

        CHECK(std::string(controller.getTrackState(t1).name) == "Audio");
        CHECK(std::string(controller.getTrackState(t2).name) == "Audio 2");
        CHECK(std::string(controller.getTrackState(t3).name) == "Audio 3");
    }

    SECTION("Renaming track handles naming collisions correctly") {
        TrackID t1 = controller.addAudioTrack("Vocal", 2, 0xFF0000FF);
        TrackID t2 = controller.addAudioTrack("Vocal 2", 2, 0xFF0000FF);

        // Rename t2 to "Vocal" -> should resolve to "Vocal 3"
        controller.renameTrack(t2, "Vocal");
        CHECK(std::string(controller.getTrackState(t2).name) == "Vocal 3");

        // Rename t1 to "Vocal" (its own current name) -> should remain "Vocal"
        controller.renameTrack(t1, "Vocal");
        CHECK(std::string(controller.getTrackState(t1).name) == "Vocal");
    }

    SECTION("Cloning track increments name suffix correctly") {
        TrackID t1 = controller.addAudioTrack("Synth", 2, 0xFF0000FF);
        
        // Clone 1 -> should yield "Synth (Clone)"
        TrackID clone1 = controller.cloneTrack(t1);
        CHECK(std::string(controller.getTrackState(clone1).name) == "Synth (Clone)");

        // Clone 2 -> should yield "Synth (Clone) 2"
        TrackID clone2 = controller.cloneTrack(t1);
        CHECK(std::string(controller.getTrackState(clone2).name) == "Synth (Clone) 2");
    }
}
