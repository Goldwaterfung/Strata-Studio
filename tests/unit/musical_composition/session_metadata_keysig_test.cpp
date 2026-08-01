#include <catch2/catch_test_macros.hpp>
#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/project_session/project_state_bridge.h"
#include "musical_composition/project_session/ikey_signature_map.h"
#include "musical_composition/project_session/project_serializer.h"
#include "musical_composition/command_history/icommand_history.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "musical_composition/interfaces/itrack_pipeline_builder.h"
#include <memory>
#include <cstring>
#include <vector>

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

TEST_CASE("Metadata target bit depth and duration + Undo/Redo", "[Layer5][ProjectSession]") {
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
    meta.targetBitDepth = 24;
    meta.sessionDurationSeconds = 0.0; // Unlimited

    // Set initial metadata (should NOT record delta since sampleRate was 0 previously)
    session->setMetadata(meta);

    // Verify initial values
    CHECK(session->getMetadata().targetBitDepth == 24);
    CHECK(session->getMetadata().sessionDurationSeconds == 0.0);

    // Update metadata (should push a delta now that sampleRate is non-zero)
    ProjectMetadata newMeta = meta;
    newMeta.targetBitDepth = 16;
    newMeta.sessionDurationSeconds = 600.0;

    session->setMetadata(newMeta);
    CHECK(session->getMetadata().targetBitDepth == 16);
    CHECK(session->getMetadata().sessionDurationSeconds == 600.0);

    // Perform Undo
    REQUIRE(history->undo() == true);
    CHECK(session->getMetadata().targetBitDepth == 24);
    CHECK(session->getMetadata().sessionDurationSeconds == 0.0);

    // Perform Redo
    REQUIRE(history->redo() == true);
    CHECK(session->getMetadata().targetBitDepth == 16);
    CHECK(session->getMetadata().sessionDurationSeconds == 600.0);
}

TEST_CASE("Key Signature Map Operations", "[Layer5][KeySignatureMap]") {
    auto builder = std::make_unique<DummyPipelineBuilder>();
    auto session = IProjectSession::create(
        std::move(builder),
        nullptr, nullptr, nullptr, NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(session != nullptr);

    auto* keyMap = session->getKeySignatureMap();
    REQUIRE(keyMap != nullptr);

    // Initially empty
    PitchClass root = PitchClass::C;
    KeyType type = KeyType::Major;
    CHECK(keyMap->getKeySignatureAt(0, root, type) == false);

    // Add key signature at 0: C Major
    keyMap->addKeySignature(0, PitchClass::C, KeyType::Major);
    
    // Query at various positions
    REQUIRE(keyMap->getKeySignatureAt(0, root, type) == true);
    CHECK(root == PitchClass::C);
    CHECK(type == KeyType::Major);

    REQUIRE(keyMap->getKeySignatureAt(1000, root, type) == true);
    CHECK(root == PitchClass::C);
    CHECK(type == KeyType::Major);

    // Query before 0 (e.g. invalid query or default fallback)
    // In our implementation, since the first is at 0, querying at 0 works, but querying before 0 returns false.
    // Let's add at 44100: A Minor
    keyMap->addKeySignature(44100, PitchClass::A, KeyType::Minor);

    // Query at 22050 (should still be C Major)
    REQUIRE(keyMap->getKeySignatureAt(22050, root, type) == true);
    CHECK(root == PitchClass::C);
    CHECK(type == KeyType::Major);

    // Query at 44100 (should be A Minor)
    REQUIRE(keyMap->getKeySignatureAt(44100, root, type) == true);
    CHECK(root == PitchClass::A);
    CHECK(type == KeyType::Minor);

    // Query at 88200 (should still be A Minor)
    REQUIRE(keyMap->getKeySignatureAt(88200, root, type) == true);
    CHECK(root == PitchClass::A);
    CHECK(type == KeyType::Minor);

    // Replace key signature at 44100 with G Major
    keyMap->addKeySignature(44100, PitchClass::G, KeyType::Major);
    REQUIRE(keyMap->getKeySignatureAt(44100, root, type) == true);
    CHECK(root == PitchClass::G);
    CHECK(type == KeyType::Major);

    // Remove key signature at 44100
    keyMap->removeKeySignature(44100);
    REQUIRE(keyMap->getKeySignatureAt(44100, root, type) == true);
    CHECK(root == PitchClass::C); // Fell back to C Major
    CHECK(type == KeyType::Major);

    // Clear all
    keyMap->clear();
    CHECK(keyMap->getKeySignatureAt(0, root, type) == false);
}

TEST_CASE("Key Signature Map Undo/Redo", "[Layer5][KeySignatureMap]") {
    auto builder = std::make_unique<DummyPipelineBuilder>();
    auto session = IProjectSession::create(
        std::move(builder),
        nullptr, nullptr, nullptr, NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(session != nullptr);

    auto* keyMap = session->getKeySignatureMap();
    auto* history = session->getCommandHistory();
    REQUIRE(keyMap != nullptr);
    REQUIRE(history != nullptr);

    // Set sample rate non-zero to enable history tracking
    ProjectMetadata meta{};
    meta.sampleRate = 44100;
    session->setMetadata(meta);
    history->clear();

    // Add Key Signature at 0
    keyMap->addKeySignature(0, PitchClass::C, KeyType::Major);
    
    // Add Key Signature at 44100 (which should be undoable)
    keyMap->addKeySignature(44100, PitchClass::A, KeyType::Minor);

    PitchClass root = PitchClass::C;
    KeyType type = KeyType::Major;
    REQUIRE(keyMap->getKeySignatureAt(44100, root, type) == true);
    CHECK(root == PitchClass::A);
    CHECK(type == KeyType::Minor);

    // Undo the second key signature addition
    REQUIRE(history->undo() == true);
    REQUIRE(keyMap->getKeySignatureAt(44100, root, type) == true);
    CHECK(root == PitchClass::C); // Back to C Major
    CHECK(type == KeyType::Major);

    // Redo the second key signature addition
    REQUIRE(history->redo() == true);
    REQUIRE(keyMap->getKeySignatureAt(44100, root, type) == true);
    CHECK(root == PitchClass::A);
    CHECK(type == KeyType::Minor);

    // Remove Key Signature at 44100
    keyMap->removeKeySignature(44100);
    REQUIRE(keyMap->getKeySignatureAt(44100, root, type) == true);
    CHECK(root == PitchClass::C);

    // Undo the removal
    REQUIRE(history->undo() == true);
    REQUIRE(keyMap->getKeySignatureAt(44100, root, type) == true);
    CHECK(root == PitchClass::A);
    CHECK(type == KeyType::Minor);
}

TEST_CASE("Project V3 Serialization & Deserialization Loop", "[Layer5][Serialization]") {
    auto builder = std::make_unique<DummyPipelineBuilder>();
    auto session = IProjectSession::create(
        std::move(builder),
        nullptr, nullptr, nullptr, NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(session != nullptr);

    ProjectMetadata meta{};
    meta.projectName = "My Serialization Project";
    meta.author = "Strata Studio";
    meta.sampleRate = 48000;
    meta.initialTempoBPM = 135.0f;
    meta.timeSignatureNumerator = 3;
    meta.timeSignatureDenominator = 4;
    meta.targetBitDepth = 32;
    meta.sessionDurationSeconds = 1200.0;
    session->setMetadata(meta);

    auto* keyMap = session->getKeySignatureMap();
    REQUIRE(keyMap != nullptr);
    keyMap->addKeySignature(0, PitchClass::D, KeyType::Major);
    keyMap->addKeySignature(96000, PitchClass::F_sharp, KeyType::Minor);

    // Serialize to buffer
    std::vector<uint8_t> buffer;
    auto* trackMgr = session->getTrackManager();
    ProjectState state = ProjectStateBridge::extract(*session, trackMgr, nullptr, "");
    REQUIRE(ProjectSerializer::serialize(state, buffer) == true);
    REQUIRE(!buffer.empty());

    // Deserialize into a fresh session
    auto builder2 = std::make_unique<DummyPipelineBuilder>();
    auto session2 = IProjectSession::create(
        std::move(builder2),
        nullptr, nullptr, nullptr, NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(session2 != nullptr);

    auto* trackMgr2 = session2->getTrackManager();
    ProjectState state2;
    REQUIRE(ProjectSerializer::deserialize(buffer, state2) == true);
    REQUIRE(ProjectStateBridge::restore(state2, *session2, trackMgr2, nullptr, nullptr, nullptr) == true);

    // Verify metadata
    const auto& meta2 = session2->getMetadata();
    CHECK(meta2.projectName == "My Serialization Project");
    CHECK(meta2.author == "Strata Studio");
    CHECK(meta2.sampleRate == 48000);
    CHECK(meta2.initialTempoBPM == 135.0f);
    CHECK(meta2.timeSignatureNumerator == 3);
    CHECK(meta2.timeSignatureDenominator == 4);
    CHECK(meta2.targetBitDepth == 32);
    CHECK(meta2.sessionDurationSeconds == 1200.0);

    // Verify Key Signature Map
    auto* keyMap2 = session2->getKeySignatureMap();
    REQUIRE(keyMap2 != nullptr);
    
    PitchClass root = PitchClass::C;
    KeyType type = KeyType::Major;
    REQUIRE(keyMap2->getKeySignatureAt(0, root, type) == true);
    CHECK(root == PitchClass::D);
    CHECK(type == KeyType::Major);

    REQUIRE(keyMap2->getKeySignatureAt(96000, root, type) == true);
    CHECK(root == PitchClass::F_sharp);
    CHECK(type == KeyType::Minor);
}

TEST_CASE("Project Serializer Rejects Legacy Header Formats", "[Layer5][Serialization]") {
    auto builder = std::make_unique<DummyPipelineBuilder>();
    auto session = IProjectSession::create(
        std::move(builder),
        nullptr, nullptr, nullptr, NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(session != nullptr);

    // Create a mock buffer with an older V2 header
    std::vector<uint8_t> mockBuffer;
    const char* header = "AGDAW_PROJ_V2";
    mockBuffer.insert(mockBuffer.end(), header, header + 13);
    
    // Add some random garbage bytes to simulate older payload
    for (int i = 0; i < 50; ++i) {
        mockBuffer.push_back(0);
    }

    ProjectState state;
    CHECK(ProjectSerializer::deserialize(mockBuffer, state) == false);
}

TEST_CASE("Project Mix Statistics Serialization & Invalidation", "[Layer5][MixStatistics]") {
    auto builder = std::make_unique<DummyPipelineBuilder>();
    auto session = IProjectSession::create(
        std::move(builder),
        nullptr, nullptr, nullptr, NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(session != nullptr);

    // Initial state
    auto initialStats = session->getMixStatistics();
    CHECK_FALSE(initialStats.isAnalyzed);
    CHECK(initialStats.integratedLoudnessLUFS == 0.0f);
    CHECK(initialStats.truePeakDBTP == 0.0f);
    CHECK_FALSE(initialStats.clippingDetected);

    // Set statistics
    MixStatistics stats{};
    stats.isAnalyzed = true;
    stats.integratedLoudnessLUFS = -14.2f;
    stats.truePeakDBTP = -0.5f;
    stats.clippingDetected = true;
    session->setMixStatistics(stats);

    auto activeStats = session->getMixStatistics();
    CHECK(activeStats.isAnalyzed);
    CHECK(activeStats.integratedLoudnessLUFS == -14.2f);
    CHECK(activeStats.truePeakDBTP == -0.5f);
    CHECK(activeStats.clippingDetected);

    // Test Invalidation via Metadata change (which pushes a delta)
    ProjectMetadata meta{};
    meta.projectName = "Initial Name";
    meta.sampleRate = 48000; // non-zero to enable history delta recording
    session->setMetadata(meta);
    
    // Set analyzed statistics again
    session->setMixStatistics(stats);
    CHECK(session->getMixStatistics().isAnalyzed);

    // Mutate metadata
    meta.projectName = "New Name";
    session->setMetadata(meta);
    
    // Verify invalidation
    CHECK_FALSE(session->getMixStatistics().isAnalyzed);

    // Reset statistics
    session->setMixStatistics(stats);

    // Test Serialization
    std::vector<uint8_t> buffer;
    auto* trackMgr = session->getTrackManager();
    ProjectState state = ProjectStateBridge::extract(*session, trackMgr, nullptr, "");
    REQUIRE(ProjectSerializer::serialize(state, buffer) == true);
    REQUIRE(!buffer.empty());

    // Deserialise into session2
    auto builder2 = std::make_unique<DummyPipelineBuilder>();
    auto session2 = IProjectSession::create(
        std::move(builder2),
        nullptr, nullptr, nullptr, NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(session2 != nullptr);

    auto* trackMgr2 = session2->getTrackManager();
    ProjectState state2;
    REQUIRE(ProjectSerializer::deserialize(buffer, state2) == true);
    REQUIRE(ProjectStateBridge::restore(state2, *session2, trackMgr2, nullptr, nullptr, nullptr) == true);

    auto stats2 = session2->getMixStatistics();
    CHECK(stats2.isAnalyzed);
    CHECK(stats2.integratedLoudnessLUFS == -14.2f);
    CHECK(stats2.truePeakDBTP == -0.5f);
    CHECK(stats2.clippingDetected);

    // Verify legacy V3 and V5 headers are rejected (no backwards compatibility in V6)
    std::vector<uint8_t> legacyBuffer = buffer;
    const char* v3Header = "AGDAW_PROJ_V3";
    std::memcpy(legacyBuffer.data(), v3Header, 13);

    ProjectState state3;
    REQUIRE(ProjectSerializer::deserialize(legacyBuffer, state3) == false);

    const char* v5Header = "AGDAW_PROJ_V5";
    std::memcpy(legacyBuffer.data(), v5Header, 13);
    REQUIRE(ProjectSerializer::deserialize(legacyBuffer, state3) == false);
}
