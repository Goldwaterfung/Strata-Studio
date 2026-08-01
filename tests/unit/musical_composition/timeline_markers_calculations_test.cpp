#include <catch2/catch_test_macros.hpp>
#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/project_session/project_state_bridge.h"
#include "musical_composition/interfaces/imarker_manager.h"
#include "musical_composition/project_session/project_serializer.h"
#include "musical_composition/command_history/icommand_history.h"
#include "musical_composition/project_session/marker_manager_impl.h"
#include "musical_composition/interfaces/itrack_pipeline_builder.h"
#include <memory>
#include <vector>
#include <cstring>

using namespace composition;

namespace {

class DummyPipelineBuilder : public ITrackPipelineBuilder {
public:
    TrackPipelineDescriptor buildPipeline(const TrackCreateInfo&, IDSPKernel*) override {
        return TrackPipelineDescriptor{};
    }
    void destroyPipeline(const TrackPipelineDescriptor&, IDSPKernel*) override {}
};

} // namespace

TEST_CASE("Timeline Markers - Core Operations & Dynamic Reindexing", "[Layer5][MarkerManager]") {
    auto builder = std::make_unique<DummyPipelineBuilder>();
    auto session = IProjectSession::create(
        std::move(builder),
        nullptr, nullptr, nullptr, NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(session != nullptr);

    auto* markerMgr = session->getMarkerManager();
    REQUIRE(markerMgr != nullptr);

    // Ensure initial state is empty
    std::vector<MarkerInfo> buffer(10);
    uint32_t count = markerMgr->getMarkersInRange(0, UINT64_MAX, buffer.data(), 10);
    CHECK(count == 0);

    // 1. Add markers out of chronological order
    // Marker A at frame 88200 (2.0 seconds at 44100Hz)
    MarkerUUID uuidA = markerMgr->addMarker(MarkerUUID{}, 88200, "Vocal Out", 0xFF00FF00);
    CHECK_FALSE(uuidA.isZero());

    // Marker B at frame 44100 (1.0 seconds at 44100Hz)
    MarkerUUID uuidB = markerMgr->addMarker(MarkerUUID{}, 44100, "Intro", 0xFFFF0000);
    CHECK_FALSE(uuidB.isZero());
    CHECK(uuidA != uuidB);

    // Query range to verify chronological sort & indexing
    count = markerMgr->getMarkersInRange(0, UINT64_MAX, buffer.data(), 10);
    REQUIRE(count == 2);

    // Marker at frame 44100 must be sorted first (Intro, index 1)
    CHECK(buffer[0].uuid == uuidB);
    CHECK(buffer[0].framePosition == 44100);
    CHECK(std::strcmp(buffer[0].label, "Intro") == 0);
    CHECK(buffer[0].markerNumber == 1);

    // Marker at frame 88200 must be sorted second (Vocal Out, index 2)
    CHECK(buffer[1].uuid == uuidA);
    CHECK(buffer[1].framePosition == 88200);
    CHECK(std::strcmp(buffer[1].label, "Vocal Out") == 0);
    CHECK(buffer[1].markerNumber == 2);

    // 2. Allow multiple markers at the same position
    MarkerUUID uuidC = markerMgr->addMarker(MarkerUUID{}, 44100, "Intro Alt", 0x0000FFFF);
    CHECK_FALSE(uuidC.isZero());
    CHECK(uuidC != uuidB);

    count = markerMgr->getMarkersInRange(0, UINT64_MAX, buffer.data(), 10);
    REQUIRE(count == 3);
    // Since we sort by position, both 44100 markers should be in the first two slots, followed by the 88200 marker.
    // Sequential numbers must still be 1, 2, 3.
    CHECK(buffer[0].markerNumber == 1);
    CHECK(buffer[1].markerNumber == 2);
    CHECK(buffer[2].markerNumber == 3);

    // 3. Remove a marker and check re-indexing
    markerMgr->removeMarker(uuidB);
    count = markerMgr->getMarkersInRange(0, UINT64_MAX, buffer.data(), 10);
    REQUIRE(count == 2);

    // The remaining markers must be reindexed consecutively: 1, 2
    CHECK(buffer[0].uuid == uuidC);
    CHECK(buffer[0].markerNumber == 1);

    CHECK(buffer[1].uuid == uuidA);
    CHECK(buffer[1].markerNumber == 2);

    // 4. Update a marker
    markerMgr->updateMarker(uuidC, 10000, "Start", 0xFFFFFFFF);
    count = markerMgr->getMarkersInRange(0, UINT64_MAX, buffer.data(), 10);
    REQUIRE(count == 2);
    CHECK(buffer[0].uuid == uuidC);
    CHECK(buffer[0].framePosition == 10000);
    CHECK(std::strcmp(buffer[0].label, "Start") == 0);
    CHECK(buffer[0].colorARGB == 0xFFFFFFFF);
    CHECK(buffer[0].markerNumber == 1);
}

TEST_CASE("Timeline Markers - Undo/Redo Operations", "[Layer5][MarkerUndoRedo]") {
    auto builder = std::make_unique<DummyPipelineBuilder>();
    auto session = IProjectSession::create(
        std::move(builder),
        nullptr, nullptr, nullptr, NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(session != nullptr);

    auto* markerMgr = session->getMarkerManager();
    auto* history = session->getCommandHistory();
    REQUIRE(markerMgr != nullptr);
    REQUIRE(history != nullptr);

    // Add marker
    MarkerUUID uuid = markerMgr->addMarker(MarkerUUID{}, 44100, "Chorus", 0xFFFF00FF);
    std::vector<MarkerInfo> buffer(5);
    REQUIRE(markerMgr->getMarkersInRange(0, UINT64_MAX, buffer.data(), 5) == 1);

    // Undo Add
    REQUIRE(history->undo() == true);
    CHECK(markerMgr->getMarkersInRange(0, UINT64_MAX, buffer.data(), 5) == 0);

    // Redo Add
    REQUIRE(history->redo() == true);
    REQUIRE(markerMgr->getMarkersInRange(0, UINT64_MAX, buffer.data(), 5) == 1);
    CHECK(buffer[0].uuid == uuid);
    CHECK(buffer[0].framePosition == 44100);
    CHECK(std::strcmp(buffer[0].label, "Chorus") == 0);

    // Update marker
    markerMgr->updateMarker(uuid, 88200, "Outro", 0xFF0000FF);
    markerMgr->getMarkersInRange(0, UINT64_MAX, buffer.data(), 5);
    CHECK(buffer[0].framePosition == 88200);
    CHECK(std::strcmp(buffer[0].label, "Outro") == 0);

    // Undo Update
    REQUIRE(history->undo() == true);
    markerMgr->getMarkersInRange(0, UINT64_MAX, buffer.data(), 5);
    CHECK(buffer[0].framePosition == 44100);
    CHECK(std::strcmp(buffer[0].label, "Chorus") == 0);

    // Redo Update
    REQUIRE(history->redo() == true);
    markerMgr->getMarkersInRange(0, UINT64_MAX, buffer.data(), 5);
    CHECK(buffer[0].framePosition == 88200);
    CHECK(std::strcmp(buffer[0].label, "Outro") == 0);

    // Remove marker
    markerMgr->removeMarker(uuid);
    CHECK(markerMgr->getMarkersInRange(0, UINT64_MAX, buffer.data(), 5) == 0);

    // Undo Remove
    REQUIRE(history->undo() == true);
    REQUIRE(markerMgr->getMarkersInRange(0, UINT64_MAX, buffer.data(), 5) == 1);
    CHECK(buffer[0].uuid == uuid);

    // Redo Remove
    REQUIRE(history->redo() == true);
    CHECK(markerMgr->getMarkersInRange(0, UINT64_MAX, buffer.data(), 5) == 0);
}

TEST_CASE("Timeline Markers - Serialization Integration", "[Layer5][MarkerSerialization]") {
    auto builder = std::make_unique<DummyPipelineBuilder>();
    auto session = IProjectSession::create(
        std::move(builder),
        nullptr, nullptr, nullptr, NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(session != nullptr);

    auto* markerMgr = session->getMarkerManager();
    // Add two markers
    MarkerUUID uuid1 = markerMgr->addMarker(MarkerUUID{}, 10000, "Marker One", 0xFF112233);
    MarkerUUID uuid2 = markerMgr->addMarker(MarkerUUID{}, 20000, "Marker Two", 0xFF445566);

    // Configure project metadata to support save format
    ProjectMetadata meta = session->getMetadata();
    meta.projectName = "Serialization Test";
    meta.sampleRate = 44100;
    session->setMetadata(meta);

    // Serialize to buffer
    std::vector<uint8_t> buffer;
    ProjectState state = ProjectStateBridge::extract(*session, session->getTrackManager(), nullptr, "test_proj.agdaw");
    REQUIRE(ProjectSerializer::serialize(state, buffer) == true);
    REQUIRE(buffer.size() > 0);

    // Deserialize into a new session
    auto builder2 = std::make_unique<DummyPipelineBuilder>();
    auto newSession = IProjectSession::create(
        std::move(builder2),
        nullptr, nullptr, nullptr, NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(newSession != nullptr);
    newSession->setMetadata(meta);

    ProjectState state2;
    REQUIRE(ProjectSerializer::deserialize(buffer, state2) == true);
    REQUIRE(ProjectStateBridge::restore(state2, *newSession, newSession->getTrackManager(), nullptr, nullptr, nullptr) == true);

    auto* newMarkerMgr = newSession->getMarkerManager();
    std::vector<MarkerInfo> restoredMarkers(5);
    uint32_t count = newMarkerMgr->getMarkersInRange(0, UINT64_MAX, restoredMarkers.data(), 5);
    REQUIRE(count == 2);

    CHECK(restoredMarkers[0].uuid == uuid1);
    CHECK(restoredMarkers[0].framePosition == 10000);
    CHECK(std::strcmp(restoredMarkers[0].label, "Marker One") == 0);
    CHECK(restoredMarkers[0].colorARGB == 0xFF112233);
    CHECK(restoredMarkers[0].markerNumber == 1);

    CHECK(restoredMarkers[1].uuid == uuid2);
    CHECK(restoredMarkers[1].framePosition == 20000);
    CHECK(std::strcmp(restoredMarkers[1].label, "Marker Two") == 0);
    CHECK(restoredMarkers[1].colorARGB == 0xFF445566);
    CHECK(restoredMarkers[1].markerNumber == 2);
}
