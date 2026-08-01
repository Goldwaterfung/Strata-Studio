#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Middle Bridge/tracks/track_controller.h"
#include "mocks/mock_track_environment.h"

TEST_CASE("MiddleBridge: Sidechain Facade and Routing Controls", "[MiddleBridge][Sidechain]") {
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

    // Create Track 1 (Kick) and Track 2 (Bass)
    TrackID kickTrack = controller.addAudioTrack("Kick", 2, 0xFF111111);
    TrackID bassTrack = controller.addAudioTrack("Bass", 2, 0xFF222222);

    SECTION("Query Available Sidechain Sources") {
        auto sources = controller.getAvailableSidechainSources(bassTrack);
        REQUIRE(sources.size() == 1);
        CHECK(sources[0].optionId == kickTrack.id);
        CHECK(sources[0].name == "Kick");

        // Target track itself must be excluded from available sources
        auto kickSources = controller.getAvailableSidechainSources(kickTrack);
        REQUIRE(kickSources.size() == 1);
        CHECK(kickSources[0].optionId == bassTrack.id);
    }

    SECTION("Set and Clear Sidechain Routing") {
        // Initial state on Bass slot 0
        auto initialState = controller.getPluginSidechainState(bassTrack, 0);
        CHECK_FALSE(initialState.isConnected);

        // Connect Kick to Bass slot 0
        controller.setPluginSidechainSource(bassTrack, 0, kickTrack, -6.0f);

        auto activeState = controller.getPluginSidechainState(bassTrack, 0);
        CHECK(activeState.isConnected);
        CHECK(activeState.sourceTrackId == kickTrack);
        CHECK(activeState.sendGaindB == Catch::Approx(-6.0f));
        CHECK(std::string(activeState.sourceTrackName) == "Kick");

        // Verify TrackUIState snapshot reflects sidechain configuration
        auto uiState = controller.getTrackState(bassTrack);
        CHECK(uiState.plugins[0].sidechain.isConnected);
        CHECK(uiState.plugins[0].sidechain.sourceTrackId == kickTrack);
        CHECK(std::string(uiState.plugins[0].sidechain.sourceTrackName) == "Kick");

        // Clear sidechain routing
        controller.clearPluginSidechainSource(bassTrack, 0);

        auto clearedState = controller.getPluginSidechainState(bassTrack, 0);
        CHECK_FALSE(clearedState.isConnected);
    }

    SECTION("Cycle Rejection in Available Sources") {
        // Connect Kick -> Bass slot 0
        controller.setPluginSidechainSource(bassTrack, 0, kickTrack, 0.0f);

        // Now if Bass is routed to Kick, querying sources for Kick should exclude Bass to prevent cycle
        auto kickSources = controller.getAvailableSidechainSources(kickTrack);
        CHECK(kickSources.empty());
    }
}
