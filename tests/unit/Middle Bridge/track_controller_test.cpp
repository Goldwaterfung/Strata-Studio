#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Middle Bridge/tracks/track_controller.h"
#include "mocks/mock_track_environment.h"

TEST_CASE("TrackController: Facade Delegation", "[MiddleBridge][TrackController]") {
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

    REQUIRE(controller.getTrackCount() == 0);
}
