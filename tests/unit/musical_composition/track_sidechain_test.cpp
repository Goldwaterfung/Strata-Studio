// tests/unit/musical_composition/track_sidechain_test.cpp

#include <catch2/catch_test_macros.hpp>
#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "musical_composition/interfaces/itrack_pipeline_builder.h"
#include "Core infrastructure/memory/istring_registry.h"
#include <memory>

using namespace composition;

namespace {

class SidechainTestPipelineBuilder : public ITrackPipelineBuilder {
public:
    TrackPipelineDescriptor buildPipeline(const TrackCreateInfo& info, IDSPKernel*) override {
        TrackPipelineDescriptor desc{};
        desc.trackNode = NodeID{100, info.trackId.id};
        desc.audioInputNode = NodeID{300, info.trackId.id};
        return desc;
    }
    void destroyPipeline(const TrackPipelineDescriptor&, IDSPKernel*) override {}
};

} // namespace

TEST_CASE("Layer 5 Sidechain System Routing & Lifecycle Teardown", "[Layer5][Sidechain]") {
    auto builder = std::make_unique<SidechainTestPipelineBuilder>();
    auto session = IProjectSession::create(
        std::move(builder),
        nullptr, nullptr, nullptr, NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(session != nullptr);

    auto* trackManager = session->getTrackManager();
    REQUIRE(trackManager != nullptr);

    // Create Track 1 (Kick) and Track 2 (Bass)
    TrackCreateInfo kickInfo{};
    kickInfo.type = TrackType::AUDIO;
    kickInfo.trackId = {1, 1};
    TrackID kickTrack = trackManager->createTrack(kickInfo);
    REQUIRE(kickTrack.isValid());

    TrackCreateInfo bassInfo{};
    bassInfo.type = TrackType::AUDIO;
    bassInfo.trackId = {2, 1};
    TrackID bassTrack = trackManager->createTrack(bassInfo);
    REQUIRE(bassTrack.isValid());

    SECTION("Establish Sidechain Routing & Query") {
        TrackID sourceTrack = TrackID::invalid();
        float gain = 0.0f;
        CHECK_FALSE(trackManager->getTrackSidechainRouting(bassTrack, 0, sourceTrack, gain));

        // Route Kick -> Bass Slot 0 Sidechain
        bool success = trackManager->setTrackSidechainRouting(bassTrack, 0, kickTrack, 1.0f);
        REQUIRE(success);

        // Verify Query
        REQUIRE(trackManager->getTrackSidechainRouting(bassTrack, 0, sourceTrack, gain));
        CHECK(sourceTrack == kickTrack);
        CHECK(gain == 1.0f);

        // Verify Pipeline Descriptor
        auto desc = trackManager->getPipelineDescriptor(bassTrack);
        CHECK(desc.sidechains[0].isEnabled == true);
        CHECK(desc.sidechains[0].sourceTrackId == kickTrack);

        // Clear Sidechain
        trackManager->clearTrackSidechainRouting(bassTrack, 0);
        CHECK_FALSE(trackManager->getTrackSidechainRouting(bassTrack, 0, sourceTrack, gain));
    }

    SECTION("Reject Self-Routing and Feedback Cycles") {
        // 1. Direct Self-Routing (Bass -> Bass)
        bool selfRoute = trackManager->setTrackSidechainRouting(bassTrack, 0, bassTrack, 1.0f);
        CHECK_FALSE(selfRoute);

        // 2. Feedback Cycle (Route Kick -> Bass output, then attempt Bass -> Kick sidechain)
        trackManager->setTrackOutputRouting(kickTrack, bassTrack); // Kick outputs to Bass
        bool cycleRoute = trackManager->setTrackSidechainRouting(kickTrack, 0, bassTrack, 1.0f); // Bass -> Kick sidechain creates loop
        CHECK_FALSE(cycleRoute);
    }

    SECTION("Automatic Cascading Teardown on Track Deletion") {
        // Route Kick -> Bass Slot 0
        REQUIRE(trackManager->setTrackSidechainRouting(bassTrack, 0, kickTrack, 0.8f));

        // Delete Kick track
        trackManager->deleteTrack(kickTrack);

        // Bass sidechain routing should be cleared automatically
        TrackID sourceTrack = TrackID::invalid();
        float gain = 0.0f;
        CHECK_FALSE(trackManager->getTrackSidechainRouting(bassTrack, 0, sourceTrack, gain));
    }
}
