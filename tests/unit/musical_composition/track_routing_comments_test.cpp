// tests/unit/musical_composition/track_routing_comments_test.cpp

#include <catch2/catch_test_macros.hpp>
#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/project_session/project_state_bridge.h"
#include "musical_composition/project_session/project_serializer.h"
#include "musical_composition/command_history/icommand_history.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "musical_composition/interfaces/itrack_pipeline_builder.h"
#include "Core infrastructure/memory/istring_registry.h"
#include <memory>
#include <vector>

using namespace composition;

namespace {

class TestPipelineBuilder : public ITrackPipelineBuilder {
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

TEST_CASE("Track Comments and Routing Integration & Undo/Redo", "[Layer5][TrackRoutingComments]") {
    auto builder = std::make_unique<TestPipelineBuilder>();
    auto session = IProjectSession::create(
        std::move(builder),
        nullptr, nullptr, nullptr, NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(session != nullptr);

    auto* trackManager = session->getTrackManager();
    auto* history = session->getCommandHistory();
    REQUIRE(trackManager != nullptr);
    REQUIRE(history != nullptr);

    // Create a track
    TrackCreateInfo trackInfo{};
    trackInfo.type = TrackType::AUDIO;
    trackInfo.trackId = {1, 1};
    trackInfo.commentsId = 0;
    
    TrackID trackId = trackManager->createTrack(trackInfo);
    REQUIRE(trackId.isValid());

    SECTION("Set Comments and Undo/Redo") {
        TrackCreateInfo info{};
        trackManager->getTrackInfo(trackId, info);
        CHECK(info.commentsId == 0);

        // Set comments (using a mock ID 42)
        trackManager->setTrackComments(trackId, 42);
        trackManager->getTrackInfo(trackId, info);
        CHECK(info.commentsId == 42);

        // Undo
        REQUIRE(history->undo() == true);
        trackManager->getTrackInfo(trackId, info);
        CHECK(info.commentsId == 0);

        // Redo
        REQUIRE(history->redo() == true);
        trackManager->getTrackInfo(trackId, info);
        CHECK(info.commentsId == 42);
    }

    SECTION("Set Output Routing and Undo/Redo") {
        // Create a target destination track
        TrackCreateInfo destInfo{};
        destInfo.type = TrackType::AUDIO;
        destInfo.trackId = {2, 1};
        
        TrackID destId = trackManager->createTrack(destInfo);
        REQUIRE(destId.isValid());

        TrackCreateInfo info{};
        trackManager->getTrackInfo(trackId, info);
        CHECK(info.outputTargetTrackId == TrackID::invalid());

        // Route track 1 to track 2
        trackManager->setTrackOutputRouting(trackId, destId);
        trackManager->getTrackInfo(trackId, info);
        CHECK(info.outputTargetTrackId == destId);

        // Undo
        REQUIRE(history->undo() == true);
        trackManager->getTrackInfo(trackId, info);
        CHECK(info.outputTargetTrackId == TrackID::invalid());

        // Redo
        REQUIRE(history->redo() == true);
        trackManager->getTrackInfo(trackId, info);
        CHECK(info.outputTargetTrackId == destId);
    }
}

TEST_CASE("Track Routing & Comments Serialization Roundtrip", "[Layer5][SerializationV5]") {
    auto stringRegistry = Layer2::IStringRegistry::create();
    REQUIRE(stringRegistry != nullptr);

    uint32_t nameId = stringRegistry->registerString("Vocals Main");
    uint32_t commentsId = stringRegistry->registerString("Vocal take 3 annotation with special properties");

    auto builder = std::make_unique<TestPipelineBuilder>();
    auto session = IProjectSession::create(
        std::move(builder),
        nullptr, nullptr, nullptr, NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(session != nullptr);
    auto* trackManager = session->getTrackManager();

    // Track 1
    TrackCreateInfo trackInfo{};
    trackInfo.type = TrackType::AUDIO;
    trackInfo.trackId = {1, 1};
    trackInfo.nameId = nameId;
    trackInfo.commentsId = commentsId;
    trackInfo.inputSourceIndex = 5;

    TrackID trackId = trackManager->createTrack(trackInfo);
    REQUIRE(trackId.isValid());

    // Track 2 (Destination)
    TrackCreateInfo destInfo{};
    destInfo.type = TrackType::AUDIO;
    destInfo.trackId = {2, 1};
    destInfo.nameId = stringRegistry->registerString("Vocal Submix");

    TrackID destId = trackManager->createTrack(destInfo);
    REQUIRE(destId.isValid());

    // Route Track 1 to Track 2
    trackManager->setTrackOutputRouting(trackId, destId);

    // Serialize
    std::vector<uint8_t> buffer;
    ProjectState state = ProjectStateBridge::extract(*session, trackManager, stringRegistry.get(), "");
    REQUIRE(ProjectSerializer::serialize(state, buffer) == true);
    REQUIRE(buffer.size() > 0);

    // Deserialize into a new session
    auto builder2 = std::make_unique<TestPipelineBuilder>();
    auto session2 = IProjectSession::create(
        std::move(builder2),
        nullptr, nullptr, nullptr, NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(session2 != nullptr);
    auto* trackManager2 = session2->getTrackManager();

    ProjectState state2;
    REQUIRE(ProjectSerializer::deserialize(buffer, state2) == true);
    REQUIRE(ProjectStateBridge::restore(state2, *session2, trackManager2, stringRegistry.get(), nullptr, nullptr) == true);

    // Verify properties
    TrackCreateInfo restoredInfo{};
    REQUIRE(trackManager2->getTrackInfo(trackId, restoredInfo) == true);
    CHECK(restoredInfo.inputSourceIndex == 5);
    CHECK(restoredInfo.outputTargetTrackId == destId);
    
    // Check comments resolution
    REQUIRE(restoredInfo.commentsId != 0);
    std::string commentsStr;
    stringRegistry->getString(restoredInfo.commentsId, commentsStr);
    CHECK(commentsStr == "Vocal take 3 annotation with special properties");
}
