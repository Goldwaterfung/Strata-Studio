#include <catch2/catch_test_macros.hpp>
#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/region_manager/iaudio_region_source_manager.h"
#include "musical_composition/command_history/icommand_history.h"
#include "musical_composition/playlist/iplaylist.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "musical_composition/interfaces/itrack_pipeline_builder.h"
#include <memory>
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

TEST_CASE("Project Metadata Undo/Redo", "[Layer5][ProjectSession]") {
    auto builder = std::make_unique<DummyPipelineBuilder>();
    auto session = IProjectSession::create(
        std::move(builder),
        nullptr, nullptr, nullptr, NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(session != nullptr);

    auto* history = session->getCommandHistory();
    REQUIRE(history != nullptr);

    ProjectMetadata meta{};
    meta.projectName = "Initial Project";
    meta.author = "Author Name";
    meta.sampleRate = 44100;
    meta.initialTempoBPM = 120.0f;
    meta.timeSignatureNumerator = 4;
    meta.timeSignatureDenominator = 4;

    // Set initial metadata (should NOT record delta since sampleRate was 0 previously)
    session->setMetadata(meta);

    // Verify initial values
    CHECK(session->getMetadata().projectName == "Initial Project");
    CHECK(history->undo() == false); // History is empty

    // Update metadata (should push a delta now that sampleRate is non-zero)
    ProjectMetadata newMeta = meta;
    newMeta.projectName = "Updated Project";
    newMeta.author = "New Author";

    session->setMetadata(newMeta);
    CHECK(session->getMetadata().projectName == "Updated Project");
    CHECK(session->getMetadata().author == "New Author");

    // Perform Undo
    REQUIRE(history->undo() == true);
    CHECK(session->getMetadata().projectName == "Initial Project");
    CHECK(session->getMetadata().author == "Author Name");

    // Perform Redo
    REQUIRE(history->redo() == true);
    CHECK(session->getMetadata().projectName == "Updated Project");
    CHECK(session->getMetadata().author == "New Author");
}

TEST_CASE("Audio Region Source Manager Reference Counting & Undo/Redo", "[Layer5][SourceManager]") {
    auto builder = std::make_unique<DummyPipelineBuilder>();
    auto session = IProjectSession::create(
        std::move(builder),
        nullptr, nullptr, nullptr, NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(session != nullptr);

    auto* sourceManager = session->getRegionSourceManager();
    auto* trackManager = session->getTrackManager();
    auto* history = session->getCommandHistory();
    REQUIRE(sourceManager != nullptr);
    REQUIRE(trackManager != nullptr);
    REQUIRE(history != nullptr);

    // Create a track to hold a playlist
    TrackCreateInfo trackInfo{};
    trackInfo.type = TrackType::AUDIO;
    trackInfo.trackId = {1, 1};
    trackInfo.audioChannelCount = 2;
    
    TrackID trackId = trackManager->createTrack(trackInfo);
    REQUIRE(trackId.isValid());

    auto* playlist = trackManager->getPlaylist(trackId);
    REQUIRE(playlist != nullptr);

    // 1. Register a source
    AudioSourceDescriptor desc{};
    desc.nameId = 123;
    desc.totalLengthSamples = 44100 * 5;
    desc.channelCount = 2;
    desc.sampleRate = 44100;
    desc.mediaId = 999;

    // Clear history to start clean
    history->clear();
    history->beginCompound();

    SourceID srcId = sourceManager->registerSource(desc, "audio_file.wav");
    REQUIRE(srcId.isValid());

    // Verify source exists in manager
    AudioSourceDescriptor outDesc{};
    REQUIRE(sourceManager->getSource(srcId, outDesc) == true);
    CHECK(outDesc.nameId == 123);

    // 2. Add an audio region using this source
    TimelineRegion region{};
    region.type = RegionType::AUDIO;
    region.sourceId = srcId;
    region.positionSample = 0;
    region.sourceStartSample = 0;
    region.sourceLength = 44100 * 5;
    
    RegionID regId = playlist->addRegion(region);
    
    history->endCompound();

    REQUIRE(regId.isValid());

    // 3. Verify reference counts and states (should be 1)
    // Undo should be possible
    CHECK(history->undo() == true);
    
    // The region is removed from the playlist.
    // The reference count drops to 0, which triggers unregisterSource, erasing the source from manager.
    REQUIRE(sourceManager->getSource(srcId, outDesc) == false);

    // Redo
    REQUIRE(history->redo() == true);
    // The region and the source are restored!
    REQUIRE(sourceManager->getSource(srcId, outDesc) == true);
    CHECK(outDesc.nameId == 123);
}

TEST_CASE("Track Lock & Performance Mode Undo/Redo", "[Layer5][TrackManager]") {
    auto builder = std::make_unique<DummyPipelineBuilder>();
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
    trackInfo.audioChannelCount = 2;
    
    TrackID trackId = trackManager->createTrack(trackInfo);
    REQUIRE(trackId.isValid());

    // Clear history to start clean
    history->clear();

    // 1. Initial State Checks
    CHECK(trackManager->isTrackLocked(trackId) == false);

    // 2. Set Track Locked (records delta)
    trackManager->setTrackLocked(trackId, true);
    CHECK(trackManager->isTrackLocked(trackId) == true);

    // Undo Lock
    REQUIRE(history->undo() == true);
    CHECK(trackManager->isTrackLocked(trackId) == false);

    // Redo Lock
    REQUIRE(history->redo() == true);
    CHECK(trackManager->isTrackLocked(trackId) == true);
}

