// tests/unit/Middle Bridge/arrangement_controller_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Middle Bridge/timeline/arrangement_controller.h"
#include "musical_composition/playlist/iplaylist.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "musical_composition/interfaces/iregion_metadata_manager.h"
#include <memory>
#include <unordered_map>
#include <algorithm>

using namespace bridge;
using namespace composition;
using namespace Layer2;

namespace Layer2 { class IEventQueue; }

namespace {

class MockPlaylist : public composition::IPlaylist {
public:
    std::vector<composition::IPlaylist::RegionInfo> regions;
    uint32_t nextRegionId = 1;

    composition::RegionID addRegion(const composition::TimelineRegion& region, composition::IPlaylist::LayerIndex layer) override {
        composition::RegionID id;
        id.id = nextRegionId++;
        id.generation = 1;
        
        composition::IPlaylist::RegionInfo info{ id, region, layer == composition::IPlaylist::AUTO_LAYER ? 0 : layer };
        regions.push_back(info);
        return id;
    }

    void removeRegion(composition::RegionID id) override {
        auto it = std::find_if(regions.begin(), regions.end(),
            [&](const composition::IPlaylist::RegionInfo& r) { return r.id == id; });
        if (it != regions.end()) {
            regions.erase(it);
        }
    }

    void moveRegion(composition::RegionID id, uint64_t newPosition, composition::IPlaylist::LayerIndex newLayer) override {
        auto it = std::find_if(regions.begin(), regions.end(),
            [&](const composition::IPlaylist::RegionInfo& r) { return r.id == id; });
        if (it != regions.end()) {
            it->region.positionSample = newPosition;
            it->layer = newLayer;
        }
    }

    void trimRegion(composition::RegionID id, uint64_t newPosition, uint64_t newSourceStart, uint64_t newSourceLength) override {
        auto it = std::find_if(regions.begin(), regions.end(),
            [&](const composition::IPlaylist::RegionInfo& r) { return r.id == id; });
        if (it != regions.end()) {
            it->region.positionSample = newPosition;
            it->region.sourceStartSample = newSourceStart;
            it->region.sourceLength = newSourceLength;
        }
    }

    composition::RegionID splitRegion(composition::RegionID id, uint64_t splitPointSample, uint64_t sourceOffsetSample = 0) override {
        auto it = std::find_if(regions.begin(), regions.end(),
            [&](const composition::IPlaylist::RegionInfo& r) { return r.id == id; });
        if (it == regions.end()) return {0, 0};

        uint64_t offset = sourceOffsetSample != 0 ? sourceOffsetSample : (splitPointSample - it->region.positionSample);
        
        composition::TimelineRegion rightRegion = it->region;
        rightRegion.positionSample = splitPointSample;
        rightRegion.sourceStartSample += offset;
        rightRegion.sourceLength -= offset;
        
        it->region.sourceLength = offset;

        composition::RegionID rightId;
        rightId.id = nextRegionId++;
        rightId.generation = 1;
        regions.push_back({ rightId, rightRegion, it->layer });
        return rightId;
    }

    void setProjectSampleRate(uint32_t) override {}

    void setFades(composition::RegionID id, uint32_t fadeIn, uint32_t fadeOut) override {
        auto it = std::find_if(regions.begin(), regions.end(),
            [&](const composition::IPlaylist::RegionInfo& r) { return r.id == id; });
        if (it != regions.end()) {
            it->region.fadeInSamples = fadeIn;
            it->region.fadeOutSamples = fadeOut;
        }
    }

    void setWarpMode(composition::RegionID id, WarpMode mode) override {
        auto it = std::find_if(regions.begin(), regions.end(),
            [&](const composition::IPlaylist::RegionInfo& r) { return r.id == id; });
        if (it != regions.end()) {
            it->region.warpMode = mode;
        }
    }

    void setPlaybackRatio(composition::RegionID id, float ratio) override {
        auto it = std::find_if(regions.begin(), regions.end(),
            [&](const composition::IPlaylist::RegionInfo& r) { return r.id == id; });
        if (it != regions.end()) {
            it->region.playbackRatio = ratio;
        }
    }

    void setSourceBpm(composition::RegionID id, float bpm) override {
        auto it = std::find_if(regions.begin(), regions.end(),
            [&](const composition::IPlaylist::RegionInfo& r) { return r.id == id; });
        if (it != regions.end()) {
            it->region.sourceBpm = bpm;
        }
    }

    void setRegionMuted(composition::RegionID id, bool muted) override {
        auto it = std::find_if(regions.begin(), regions.end(),
            [&](const composition::IPlaylist::RegionInfo& r) { return r.id == id; });
        if (it != regions.end()) {
            it->region.isMuted = muted;
        }
    }

    void setRegionGain(composition::RegionID id, float gain) override {
        auto it = std::find_if(regions.begin(), regions.end(),
            [&](const composition::IPlaylist::RegionInfo& r) { return r.id == id; });
        if (it != regions.end()) {
            it->region.gain = gain;
        }
    }



    uint32_t getRegionsAt(uint64_t samplePos, composition::TimelineRegion* outRegions, uint32_t maxRegions) const override {
        uint32_t count = 0;
        for (const auto& r : regions) {
            if (samplePos >= r.region.positionSample && samplePos < r.region.positionSample + r.region.sourceLength) {
                if (count < maxRegions) {
                    outRegions[count] = r.region;
                    count++;
                }
            }
        }
        return count;
    }

    uint32_t getAllRegions(composition::IPlaylist::RegionInfo* outRegions, uint32_t maxRegions) const override {
        if (!outRegions || maxRegions == 0) {
            return static_cast<uint32_t>(regions.size());
        }
        uint32_t count = 0;
        for (const auto& r : regions) {
            if (count < maxRegions) {
                outRegions[count] = r;
                count++;
            }
        }
        return count;
    }
    
    uint32_t getMaxLayer() const override { return 1; }
};

class MockTrackManager : public ITrackManager {
public:
    std::unordered_map<uint32_t, std::unique_ptr<MockPlaylist>> playlists;
    std::vector<TrackID> allTrackIds;

    TrackID createTrack(const TrackCreateInfo& /*info*/) override { return {0, 0}; }
    void deleteTrack(TrackID /*id*/) override {}
    void renameTrack(TrackID /*id*/, uint32_t /*newNameId*/) override {}
    void setTrackComments(TrackID /*id*/, uint32_t /*commentsId*/) override {}
    void setTrackOutputRouting(TrackID /*id*/, TrackID /*destinationTrackId*/) override {}
    void moveTrack(TrackID /*id*/, uint32_t /*newIndexPosition*/, TrackID /*newParentFolderId*/) override {}
    void setTrackColor(TrackID /*id*/, uint32_t /*newColorARGB*/) override {}
    void setTrackRecordArmed(TrackID /*id*/, bool /*armed*/) override {}
    void setTrackInputMonitoring(TrackID /*id*/, bool /*enabled*/) override {}
    void setTrackType(TrackID /*id*/, TrackType /*type*/) override {}
    void setTrackTakesExpanded(TrackID, bool) override {}
    void setTrackLocked(TrackID, bool) override {}
    bool isTrackLocked(TrackID) const override { return false; }
    
    IPlaylist* getPlaylist(TrackID id) override {
        auto it = playlists.find(id.id);
        if (it != playlists.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    IMIDISequencer* getMIDISequencer(TrackID /*id*/) override { return nullptr; }
    IAutomationLaneManager* getAutomationManager(TrackID) override { return nullptr; }

    std::atomic<uint64_t>* getRecordingStartSample(TrackID) override { return nullptr; }
    TrackPipelineDescriptor getPipelineDescriptor(TrackID /*id*/) const override { return {}; }
    NodeID getTrackOutputNode(TrackID /*id*/) const override { return NodeID::invalid(); }
    
    std::vector<TrackID> getAllTrackIDs() const override {
        return allTrackIds;
    }

    bool getTrackInfo(TrackID id, TrackCreateInfo& outInfo) const override {
        auto it = std::find_if(allTrackIds.begin(), allTrackIds.end(), [&](const TrackID& t) { return t.id == id.id; });
        if (it != allTrackIds.end()) {
            outInfo.nameId = 1;
            outInfo.colorARGB = 0xFF112233;
            return true;
        }
        return false;
    }
    uint32_t getTrackIndexPosition(TrackID /*id*/) const override { return 0; }
    TrackID getTrackParentFolderId(TrackID /*id*/) const override { return {0, 0}; }

    void renderMIDIPlayback(
        uint64_t,
        uint32_t,
        bool,
        uint64_t,
        uint64_t,
        Layer2::IEventQueue*,
        bool
    ) override {}
    void compileTimelineSnapshot() override {}
    void setProjectSampleRate(uint32_t) override {}
    void recalculateTimeCaches(Layer2::ITempoService*) override {}

    // Mixer Operations
    void setTrackFaderGain(TrackID, float) override {}
    void setTrackPan(TrackID, float) override {}
    void setTrackMute(TrackID, bool) override {}
    void setTrackSolo(TrackID, bool) override {}

    // Mixer Queries
    float getTrackFaderGain(TrackID) const override { return 1.0f; }
    float getTrackPan(TrackID) const override { return 0.5f; }
    bool getTrackMute(TrackID) const override { return false; }
    bool getTrackSolo(TrackID) const override { return false; }

    // Routing Operations
    void setTrackSendGain(TrackID, bool, uint32_t, float) override {}
    void setTrackSendPan(TrackID, bool, uint32_t, float) override {}
    void setTrackSendEnabled(TrackID, bool, uint32_t, bool) override {}
    void setTrackSendDestination(TrackID, bool, uint32_t, NodeID) override {}
    void setTrackAudioInputChannel(TrackID, uint32_t, uint32_t) override {}

    // Routing Queries
    float getTrackSendGain(TrackID, bool, uint32_t) const override { return 0.0f; }
    float getTrackSendPan(TrackID, bool, uint32_t) const override { return 0.5f; }
    bool getTrackSendEnabled(TrackID, bool, uint32_t) const override { return false; }
    NodeID getTrackSendDestination(TrackID, bool, uint32_t) const override { return NodeID::invalid(); }
    std::string getTrackSendDestinationName(TrackID, bool, uint32_t) const override { return ""; }

    // Plugin Operations
    void insertTrackPlugin(TrackID, uint32_t, uint32_t) override {}
    void removeTrackPlugin(TrackID, uint32_t) override {}
    void setTrackPluginBypassed(TrackID, uint32_t, bool) override {}
    void insertTrackInstrument(TrackID, uint32_t) override {}
    void removeTrackInstrument(TrackID) override {}
    void setTrackInstrumentBypassed(TrackID, bool) override {}
    void completeTrackInstrumentInsertion(TrackID, void*, const struct PluginDescriptor&) override {}
    void completeTrackPluginInsertion(TrackID, uint32_t, void*, const struct PluginDescriptor&) override {}

    // Sidechain Operations
    bool setTrackSidechainRouting(TrackID, uint32_t, TrackID, float) override { return true; }
    void clearTrackSidechainRouting(TrackID, uint32_t) override {}
    bool getTrackSidechainRouting(TrackID, uint32_t, TrackID&, float&) const override { return false; }
    bool detectFeedbackCycle(TrackID, TrackID) const override { return false; }

    // Callback Registration
    void registerMixerRoutingCallback(MixerRoutingCallback) override {}
};

class MockSourceManager : public IAudioRegionSourceManager {
public:
    std::unordered_map<uint32_t, AudioSourceDescriptor> sources;
    uint32_t nextId = 1;

    SourceID registerSource(const AudioSourceDescriptor& descriptor, const std::string& /*filePath*/ = "") override {
        SourceID id;
        id.id = nextId++;
        id.generation = 1;
        
        AudioSourceDescriptor d = descriptor;
        d.sourceId = id;
        sources[id.id] = d;
        return id;
    }

    bool getSource(SourceID id, AudioSourceDescriptor& outDescriptor) const override {
        auto it = sources.find(id.id);
        if (it != sources.end()) {
            outDescriptor = it->second;
            return true;
        }
        return false;
    }

    void incrementReference(SourceID /*id*/) override {}
    void decrementReference(SourceID /*id*/) override {}
};

class MockStringRegistry : public IStringRegistry {
public:
    std::unordered_map<uint32_t, std::string> strings;
    uint32_t nextId = 1;

    uint32_t registerString(const std::string& str) override {
        for (const auto& pair : strings) {
            if (pair.second == str) return pair.first;
        }
        uint32_t id = nextId++;
        strings[id] = str;
        return id;
    }

    bool getString(uint32_t id, std::string& outStr) const override {
        auto it = strings.find(id);
        if (it != strings.end()) {
            outStr = it->second;
            return true;
        }
        return false;
    }

    void unregisterString(uint32_t /*id*/) override {}
};

class MockRegionMetadataManager : public composition::IRegionMetadataManager {
public:
    std::unordered_map<uint64_t, composition::RegionMetadata> metadataMap;
    void getRegionMetadata(RegionID id, composition::RegionMetadata& outMeta) const override {
        auto it = metadataMap.find(id.toRaw());
        if (it != metadataMap.end()) {
            outMeta = it->second;
        }
    }
    bool hasRegionMetadata(RegionID id) const override {
        return metadataMap.find(id.toRaw()) != metadataMap.end();
    }
    void setRegionMetadata(RegionID id, const composition::RegionMetadata& meta, bool) override {
        metadataMap[id.toRaw()] = meta;
    }
    void removeRegionMetadata(RegionID id, bool) override {
        metadataMap.erase(id.toRaw());
    }
    void clear() override {
        metadataMap.clear();
    }
};

class MockProjectSession : public composition::IProjectSession {
public:
    explicit MockProjectSession(composition::ITrackManager* tm, composition::IAudioRegionSourceManager* sm)
        : trackManager(tm), sourceManager(sm) {
        metadata.sampleRate = 44100;
        metadata.initialTempoBPM = 120.0f;
    }

    composition::ITrackManager* getTrackManager() override { return trackManager; }
    composition::IArrangementManager* getArrangementManager() override { return nullptr; }
    composition::IAudioRegionSourceManager* getRegionSourceManager() override { return sourceManager; }
    composition::IRegionMetadataManager* getRegionMetadataManager() override { return &metaManager; }

    composition::ICommandHistory* getCommandHistory() override { return nullptr; }
    composition::IArrangerTrack* getArrangerTrack() override { return nullptr; }
    composition::IChordTrack* getChordTrack() override { return nullptr; }
    composition::IClipboard* getClipboard() override { return nullptr; }
    composition::ICompingEngine* getCompingEngine() override { return nullptr; }
    composition::IMarkerManager* getMarkerManager() override { return nullptr; }
    composition::IKeySignatureMap* getKeySignatureMap() override { return nullptr; }

    const composition::ProjectMetadata& getMetadata() const override { return metadata; }
    void setMetadata(const composition::ProjectMetadata& meta) override { metadata = meta; }

    composition::MixStatistics getMixStatistics() const override { return mixStats; }
    void setMixStatistics(const composition::MixStatistics& stats) override { mixStats = stats; }

    bool saveToFile(const std::string&, Layer2::IStringRegistry* = nullptr) override { return true; }
    bool loadFromFile(const std::string&, Layer2::IStringRegistry* = nullptr, std::vector<composition::MissingPluginReport>* = nullptr) override { return true; }
    bool saveToJsonFile(const std::string&, Layer2::IStringRegistry* = nullptr) override { return true; }
    bool loadFromJsonFile(const std::string&, Layer2::IStringRegistry* = nullptr, std::vector<composition::MissingPluginReport>* = nullptr) override { return true; }

    composition::ProjectMetadata metadata;
    composition::MixStatistics mixStats;
    composition::ITrackManager* trackManager;
    composition::IAudioRegionSourceManager* sourceManager;
    MockRegionMetadataManager metaManager;
};

class MockSessionManager : public bridge::ISessionManager {
public:
    void registerChangeListener(bridge::ISessionChangeListener* listener) override {
        listeners.push_back(listener);
    }
    void unregisterChangeListener(bridge::ISessionChangeListener* listener) override {
        listeners.erase(std::remove(listeners.begin(), listeners.end(), listener), listeners.end());
    }
    composition::IProjectSession* getActiveSession() const override {
        return activeSession;
    }
    void setActiveSession(std::unique_ptr<composition::IProjectSession> session) override {
        for (auto* l : listeners) l->onSessionChanging();
        activeSession = session.get();
        ownedSession = std::move(session);
        for (auto* l : listeners) l->onSessionChanged(activeSession);
    }
    void closeActiveSession() override {
        for (auto* l : listeners) l->onSessionChanging();
        activeSession = nullptr;
        ownedSession.reset();
        for (auto* l : listeners) l->onSessionChanged(nullptr);
    }
    void triggerSessionRefresh() override {}
    void onTempoMapChanged(Layer2::ITempoService*) override {}
    Layer2::IStringRegistry* getStringRegistry() const override { return nullptr; }


    std::vector<bridge::ISessionChangeListener*> listeners;
    composition::IProjectSession* activeSession = nullptr;
    std::unique_ptr<composition::IProjectSession> ownedSession;
};

} // namespace

TEST_CASE("ArrangementController: Basic Clip Import and Mutations", "[bridge][ArrangementController]") {
    MockTrackManager trackManager;
    MockSourceManager sourceManager;
    MockStringRegistry stringRegistry;
    MockSessionManager sessionManager;
    auto session = std::make_unique<MockProjectSession>(&trackManager, &sourceManager);
    sessionManager.setActiveSession(std::move(session));

    TrackID track1{1, 1};
    trackManager.allTrackIds.push_back(track1);
    trackManager.playlists[track1.id] = std::make_unique<MockPlaylist>();

    ArrangementController controller(&sessionManager, &stringRegistry);

    SECTION("Import Audio Clip") {
        RegionID rId = controller.importAudioClip(track1, "/path/to/guitar.wav", 44100);
        REQUIRE(rId.id != 0);

        auto* playlist = trackManager.playlists[track1.id].get();
        REQUIRE(playlist->regions.size() == 1);
        CHECK(playlist->regions[0].id == rId);
        CHECK(playlist->regions[0].region.positionSample == 44100);
        CHECK(playlist->regions[0].region.sourceLength == 441000);

        // Check source registry mapping
        AudioSourceDescriptor desc;
        bool foundSource = sourceManager.getSource(playlist->regions[0].region.sourceId, desc);
        REQUIRE(foundSource);
        CHECK(desc.totalLengthSamples == 441000);

        // Check file path resolve
        std::string resolvedPath;
        bool foundPath = stringRegistry.getString(desc.nameId, resolvedPath);
        REQUIRE(foundPath);
        CHECK(resolvedPath == "/path/to/guitar.wav");
    }



    SECTION("Delete Region") {
        RegionID rId = controller.importAudioClip(track1, "/path/to/drums.wav", 0);
        REQUIRE(trackManager.playlists[track1.id]->regions.size() == 1);

        controller.deleteRegion(rId);
        CHECK(trackManager.playlists[track1.id]->regions.empty());
    }

    SECTION("Split Region") {
        RegionID rId = controller.importAudioClip(track1, "/path/to/synth.wav", 10000);
        REQUIRE(trackManager.playlists[track1.id]->regions.size() == 1);

        // Split region at timeline frame 15000 (offset of 5000)
        controller.splitRegion(rId, 15000);
        auto* playlist = trackManager.playlists[track1.id].get();
        REQUIRE(playlist->regions.size() == 2);

        // Find the split parts
        auto leftPart = playlist->regions[0];
        auto rightPart = playlist->regions[1];

        CHECK(leftPart.region.positionSample == 10000);
        CHECK(leftPart.region.sourceLength == 5000);

        CHECK(rightPart.region.positionSample == 15000);
        CHECK(rightPart.region.sourceLength == 441000 - 5000);
        CHECK(rightPart.region.sourceStartSample == 5000);
    }
}

TEST_CASE("ArrangementController: Same-Track and Cross-Track Moving", "[bridge][ArrangementController]") {
    MockTrackManager trackManager;
    MockSourceManager sourceManager;
    MockStringRegistry stringRegistry;
    MockSessionManager sessionManager;
    auto session = std::make_unique<MockProjectSession>(&trackManager, &sourceManager);
    sessionManager.setActiveSession(std::move(session));

    TrackID track1{1, 1};
    TrackID track2{2, 1};
    trackManager.allTrackIds.push_back(track1);
    trackManager.allTrackIds.push_back(track2);
    trackManager.playlists[track1.id] = std::make_unique<MockPlaylist>();
    trackManager.playlists[track2.id] = std::make_unique<MockPlaylist>();

    ArrangementController controller(&sessionManager, &stringRegistry);
    RegionID rId = controller.importAudioClip(track1, "/path/to/vocals.wav", 20000);

    SECTION("Move region on same track") {
        controller.moveRegion(rId, track1, 35000);
        auto* playlist = trackManager.playlists[track1.id].get();
        REQUIRE(playlist->regions.size() == 1);
        CHECK(playlist->regions[0].region.positionSample == 35000);
    }

    SECTION("Move region to negative position clamps to 0") {
        controller.moveRegion(rId, track1, -10000);
        auto* playlist = trackManager.playlists[track1.id].get();
        REQUIRE(playlist->regions.size() == 1);
        CHECK(playlist->regions[0].region.positionSample == 0);
    }

    SECTION("Move region to a different track") {
        controller.moveRegion(rId, track2, 50000);
        
        auto* playlist1 = trackManager.playlists[track1.id].get();
        auto* playlist2 = trackManager.playlists[track2.id].get();

        CHECK(playlist1->regions.empty());
        REQUIRE(playlist2->regions.size() == 1);
        CHECK(playlist2->regions[0].region.positionSample == 50000);
    }
}

TEST_CASE("ArrangementController: High-Performance Viewport Culling", "[bridge][ArrangementController]") {
    MockTrackManager trackManager;
    MockSourceManager sourceManager;
    MockStringRegistry stringRegistry;
    MockSessionManager sessionManager;
    auto session = std::make_unique<MockProjectSession>(&trackManager, &sourceManager);
    sessionManager.setActiveSession(std::move(session));

    TrackID track1{1, 1};
    trackManager.allTrackIds.push_back(track1);
    trackManager.playlists[track1.id] = std::make_unique<MockPlaylist>();

    ArrangementController controller(&sessionManager, &stringRegistry);

    // Import multiple clips:
    // Region A: pos = 10,000, len = 441,000 (ends 451,000)
    RegionID rIdA = controller.importAudioClip(track1, "/path/to/A.wav", 10000);
    // Region B: pos = 500,000, len = 441,000 (ends 941,000)
    RegionID rIdB = controller.importAudioClip(track1, "/path/to/B.wav", 500000);

    REQUIRE(rIdA.id != 0);
    REQUIRE(rIdB.id != 0);

    SECTION("Viewport culling: only A should be visible") {
        VisualRegion results[10];
        uint32_t count = controller.getRegionsInViewport(0, 200000, results, 10);
        REQUIRE(count == 1);
        CHECK(results[0].id == rIdA);
        CHECK(results[0].trackId == track1);
        CHECK(std::string(results[0].name) == "A.wav");
        CHECK(results[0].startFrame == 10000);
    }

    SECTION("Viewport culling: both A and B should be visible") {
        VisualRegion results[10];
        uint32_t count = controller.getRegionsInViewport(100000, 600000, results, 10);
        REQUIRE(count == 2);
        
        // Ensure both regions are in results (order might vary)
        bool hasA = (results[0].id == rIdA || results[1].id == rIdA);
        bool hasB = (results[0].id == rIdB || results[1].id == rIdB);
        CHECK(hasA);
        CHECK(hasB);
    }

    SECTION("Viewport culling: none visible") {
        VisualRegion results[10];
        uint32_t count = controller.getRegionsInViewport(1000000, 1200000, results, 10);
        CHECK(count == 0);
    }
}
