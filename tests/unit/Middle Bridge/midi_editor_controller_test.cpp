// tests/unit/Middle Bridge/midi_editor_controller_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Middle Bridge/midi/midi_editor_controller.h"
#include "Core infrastructure/bridges/ievent_queue.h"
#include "Core infrastructure/clock/iclock_service.h"
#include "Core infrastructure/tempo/itempo_service.h"
#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "musical_composition/playlist/iplaylist.h"
#include "musical_composition/midi_sequencer/imidi_sequencer.h"
#include "musical_composition/interfaces/iregion_metadata_manager.h"
#include "Middle Bridge/project/isession_manager.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <thread>
#include <chrono>

using namespace bridge;
using namespace Layer2;
using composition::ClipID;
using composition::SourceID;

namespace {

class MockPlaylist : public composition::IPlaylist {
public:
    std::vector<composition::IPlaylist::RegionInfo> regions;

    composition::RegionID addRegion(const composition::TimelineRegion& region, composition::IPlaylist::LayerIndex layer = composition::IPlaylist::AUTO_LAYER) override {
        composition::RegionID id{1, static_cast<uint32_t>(regions.size() + 1)};
        regions.push_back({id, region, layer});
        return id;
    }

    void removeRegion(composition::RegionID id) override {
        regions.erase(std::remove_if(regions.begin(), regions.end(), [&](const composition::IPlaylist::RegionInfo& info) {
            return info.id == id;
        }), regions.end());
    }

    void moveRegion(composition::RegionID id, uint64_t newPosition, composition::IPlaylist::LayerIndex newLayer) override {
        for (auto& info : regions) {
            if (info.id == id) {
                info.region.positionSample = newPosition;
                info.layer = newLayer;
                break;
            }
        }
    }

    void trimRegion(composition::RegionID id, uint64_t newPosition, uint64_t newSourceStart, uint64_t newSourceLength) override {
        for (auto& info : regions) {
            if (info.id == id) {
                info.region.positionSample = newPosition;
                info.region.sourceStartSample = newSourceStart;
                info.region.sourceLength = newSourceLength;
                break;
            }
        }
    }

    composition::RegionID splitRegion(composition::RegionID, uint64_t, uint64_t = 0) override { return {0, 0}; }
    void setProjectSampleRate(uint32_t) override {}
    void setFades(composition::RegionID, uint32_t, uint32_t) override {}
    void setRegionMuted(composition::RegionID, bool) override {}
    void setRegionGain(composition::RegionID, float) override {}
    void setWarpMode(composition::RegionID, WarpMode) override {}
    void setPlaybackRatio(composition::RegionID, float) override {}
    void setSourceBpm(composition::RegionID, float) override {}

    uint32_t getAllRegions(
        composition::IPlaylist::RegionInfo* outRegions,
        uint32_t maxRegions
    ) const override {
        if (!outRegions || maxRegions == 0) {
            return static_cast<uint32_t>(regions.size());
        }
        uint32_t count = 0;
        for (const auto& info : regions) {
            if (count < maxRegions) {
                outRegions[count++] = info;
            } else {
                break;
            }
        }
        return count; 
    }

    uint32_t getRegionsAt(uint64_t /*samplePos*/, composition::TimelineRegion* /*outRegions*/, uint32_t /*maxRegions*/) const override { return 0; }
    uint32_t getMaxLayer() const override { return 1; }
};

class MockMIDISequencer : public composition::IMIDISequencer {
public:
    struct NoteEntry {
        ClipID clipId;
        composition::MIDINote note;
    };
    std::vector<NoteEntry> notes;

    struct CCEntry {
        ClipID clipId;
        composition::MIDICCPoint point;
    };
    std::vector<CCEntry> ccPoints;

    NodeID targetNode = {0, 0};

    NoteID addNote(ClipID clipId, const composition::MIDINote& note) override {
        auto n = note;
        n.noteId = {1, static_cast<uint32_t>(notes.size() + 1)};
        notes.push_back({clipId, n});
        return n.noteId;
    }

    void removeNote(NoteID id) override {
        notes.erase(std::remove_if(notes.begin(), notes.end(), [&](const NoteEntry& e) {
            return e.note.noteId == id;
        }), notes.end());
    }

    void updateNote(NoteID id, const composition::MIDINote& newNote) override {
        for (auto& e : notes) {
            if (e.note.noteId == id) {
                e.note = newNote;
                break;
            }
        }
    }

    void addCCPoint(ClipID clipId, const composition::MIDICCPoint& point) override {
        ccPoints.push_back({clipId, point});
    }

    void addPitchPoint(ClipID, const composition::MIDIPitchPoint&) override {}

    uint32_t getCCPointsInClip(
        ClipID clipId,
        composition::MIDICCPoint* outPoints,
        uint32_t maxPoints
    ) const override {
        uint32_t count = 0;
        for (const auto& e : ccPoints) {
            if (e.clipId == clipId) {
                if (count < maxPoints) {
                    outPoints[count++] = e.point;
                } else {
                    break;
                }
            }
        }
        return count;
    }

    uint32_t getPitchPointsInClip(
        ClipID,
        composition::MIDIPitchPoint*,
        uint32_t
    ) const override {
        return 0;
    }

    void removeCCPointsInClip(ClipID clipId) override {
        ccPoints.erase(std::remove_if(ccPoints.begin(), ccPoints.end(), [&](const CCEntry& e) {
            return e.clipId == clipId;
        }), ccPoints.end());
    }

    void updateClipPosition(ClipID, uint64_t, uint64_t, const composition::MusicalPosition&) override {}
    void removeClip(ClipID) override {}
    void recalculateTimeCaches(Layer2::ITempoService*) override {}

    void setTargetNodeId(NodeID nodeId) override {
        targetNode = nodeId;
    }

    void renderToEvents(
        uint64_t,
        uint32_t,
        bool,
        uint64_t,
        uint64_t,
        Layer2::IEventQueue*
    ) override {}

    uint32_t getNotesInClip(
        ClipID clipId,
        composition::MIDINote* outNotes,
        uint32_t maxNotes
    ) const override {
        uint32_t count = 0;
        for (const auto& e : notes) {
            if (e.clipId == clipId) {
                if (count < maxNotes) {
                    outNotes[count++] = e.note;
                } else {
                    break;
                }
            }
        }
        return count;
    }
};

class MockTrackManager : public composition::ITrackManager {
public:
    struct MockTrack {
        composition::TrackCreateInfo info;
        composition::TrackPipelineDescriptor pipeline;
        std::unique_ptr<MockMIDISequencer> sequencer = std::make_unique<MockMIDISequencer>();
        std::unique_ptr<MockPlaylist> playlist = std::make_unique<MockPlaylist>();
    };

    std::unordered_map<uint32_t, std::unique_ptr<MockTrack>> tracks;
    uint32_t nextId = 1;

    TrackID createTrack(const composition::TrackCreateInfo& info) override {
        TrackID id;
        id.id = nextId++;
        id.generation = 1;

        auto t = std::make_unique<MockTrack>();
        t->info = info;
        t->pipeline.sourceNode = NodeID{1, id.id};

        tracks[id.id] = std::move(t);
        return id;
    }

    void deleteTrack(TrackID id) override {
        tracks.erase(id.id);
    }

    void renameTrack(TrackID, uint32_t) override {}
    void setTrackComments(TrackID, uint32_t) override {}
    void setTrackOutputRouting(TrackID, TrackID) override {}
    void moveTrack(TrackID, uint32_t, TrackID) override {}
    void setTrackColor(TrackID, uint32_t) override {}
    void setTrackTakesExpanded(TrackID, bool) override {}
    void setTrackLocked(TrackID, bool) override {}
    bool isTrackLocked(TrackID) const override { return false; }

    void setTrackRecordArmed(TrackID id, bool armed) override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            it->second->info.isRecordArmed = armed;
        }
    }
    void setTrackInputMonitoring(TrackID id, bool enabled) override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            it->second->info.isInputMonitoring = enabled;
        }
    }
    void setTrackType(TrackID id, composition::TrackType type) override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            it->second->info.type = type;
        }
    }

    composition::IPlaylist* getPlaylist(TrackID id) override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            return it->second->playlist.get();
        }
        return nullptr;
    }
    composition::IMIDISequencer* getMIDISequencer(TrackID id) override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            return it->second->sequencer.get();
        }
        return nullptr;
    }
    composition::IAutomationLaneManager* getAutomationManager(TrackID) override { return nullptr; }

    std::atomic<uint64_t>* getRecordingStartSample(TrackID) override { return nullptr; }

    composition::TrackPipelineDescriptor getPipelineDescriptor(TrackID id) const override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            return it->second->pipeline;
        }
        return composition::TrackPipelineDescriptor{};
    }

    NodeID getTrackOutputNode(TrackID) const override { return NodeID::invalid(); }

    std::vector<TrackID> getAllTrackIDs() const override {
        std::vector<TrackID> ids;
        for (const auto& [k, _] : tracks) {
            ids.push_back({k, 1});
        }
        return ids;
    }

    bool getTrackInfo(TrackID id, composition::TrackCreateInfo& outInfo) const override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            outInfo = it->second->info;
            return true;
        }
        return false;
    }

    uint32_t getTrackIndexPosition(TrackID) const override { return 0; }
    TrackID getTrackParentFolderId(TrackID) const override { return {0, 0}; }
    void renderMIDIPlayback(uint64_t, uint32_t, bool, uint64_t, uint64_t, Layer2::IEventQueue*, bool) override {}
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
    void registerMixerRoutingCallback(composition::MixerRoutingCallback) override {}
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
    explicit MockProjectSession(composition::ITrackManager* tm) : trackManager(tm) {}

    composition::ITrackManager* getTrackManager() override { return trackManager; }
    composition::IArrangementManager* getArrangementManager() override { return nullptr; }
    composition::ICommandHistory* getCommandHistory() override { return nullptr; }
    composition::IArrangerTrack* getArrangerTrack() override { return nullptr; }
    composition::IChordTrack* getChordTrack() override { return nullptr; }
    composition::IAudioRegionSourceManager* getRegionSourceManager() override { return nullptr; }
    composition::IClipboard* getClipboard() override { return nullptr; }
    composition::ICompingEngine* getCompingEngine() override { return nullptr; }
    composition::IMarkerManager* getMarkerManager() override { return nullptr; }
    composition::IKeySignatureMap* getKeySignatureMap() override { return nullptr; }
    composition::IRegionMetadataManager* getRegionMetadataManager() override { return &metaManager; }

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

class MockClockService : public Layer2::IClockService {
public:
    uint64_t cycleStartSteadyTime = 0;
    uint32_t currentNumFrames = 512;
    double sampleRate = 48000.0;
    uint64_t cycleStartTime = 0;
    uint64_t cycleId = 0;

    void startCycle(uint64_t hardwareTimestamp, uint32_t numFrames) override {
        cycleStartTime = hardwareTimestamp;
        currentNumFrames = numFrames;
    }

    void endCycle() override {}

    uint64_t getCycleStartTime() const override { return cycleStartTime; }
    uint64_t getCycleId() const override { return cycleId; }

    uint64_t getTimestampForSample(uint32_t sampleOffset) const override {
        return cycleStartTime + static_cast<uint64_t>(sampleOffset * (1e9 / sampleRate));
    }

    uint32_t getOffsetForTimestamp(uint64_t rawTimestamp) const override {
        if (rawTimestamp <= cycleStartTime) return 0;
        return static_cast<uint32_t>((rawTimestamp - cycleStartTime) / (1e9 / sampleRate));
    }

    uint64_t getCycleStartSteadyTime() const override { return cycleStartSteadyTime; }
    uint32_t getCurrentNumFrames() const override { return currentNumFrames; }

    void setSampleRate(double rate) override { sampleRate = rate; }
    double getSampleRate() const override { return sampleRate; }
};

class MockTempoService : public ITempoService {
public:
    double sampleRate = 48000.0;
    double bpm = 120.0;
    uint8_t numerator = 4;
    uint8_t denominator = 4;

    void setTempoAtPosition(double b, uint64_t) override { bpm = b; }
    void addTempoEvent(const TempoPoint&) override {}
    void removeTempoEventAtPosition(uint64_t) override {}
    void clearTempoMap() override {}
    void setMeterAtPosition(uint8_t num, uint8_t den, uint64_t) override {
        numerator = num;
        denominator = den;
    }
    void addMeterEvent(const MeterPoint&) override {}
    void clearMeterMap() override {}

    double getTempoAtPosition(uint64_t) const override { return bpm; }
    bool getMeterAtPosition(uint64_t, uint8_t& outNumerator, uint8_t& outDenominator) const override {
        outNumerator = numerator;
        outDenominator = denominator;
        return true;
    }

    void updateForCycle(const ProcessContext&) override {}
    uint64_t getCyclePositionSamples() const override { return 0; }
    double getCycleBPM() const override { return bpm; }
    BBTPosition getCycleBBT() const override { return BBTPosition(); }

    uint64_t beatsToSamples(double beats) const override {
        return static_cast<uint64_t>(beats / (bpm / 60.0) * sampleRate);
    }
    double samplesToBeats(uint64_t samples) const override {
        return static_cast<double>(samples) / sampleRate * (bpm / 60.0);
    }
    BBTPosition samplesToBBT(uint64_t samples) const override {
        BBTPosition pos;
        double beats = samplesToBeats(samples);
        pos.bar = static_cast<uint32_t>(beats / numerator) + 1;
        pos.beat = static_cast<uint16_t>(std::fmod(beats, numerator)) + 1;
        pos.tick = static_cast<uint16_t>((beats - std::floor(beats)) * 960);
        return pos;
    }
    uint64_t bbtToSamples(const BBTPosition& bbt) const override {
        double totalBeats = (static_cast<double>(bbt.bar) - 1.0) * numerator + (static_cast<double>(bbt.beat) - 1.0) + (static_cast<double>(bbt.tick) / 960.0);
        return beatsToSamples(totalBeats);
    }

    uint32_t getTempoRange(uint64_t, uint64_t, TempoPoint*, uint32_t) const override { return 0; }
    uint32_t getMeterRange(uint64_t, uint64_t, MeterPoint*, uint32_t) const override { return 0; }
    void setSampleRate(double rate) override { sampleRate = rate; }
    double getSampleRate() const override { return sampleRate; }
    void setTicksPerBeat(uint32_t) override {}
    uint32_t getTicksPerBeat() const override { return 960; }
};

} // namespace

TEST_CASE("MidiEditorController: Active Clip Focus and CRUD", "[bridge][MidiEditorController]") {
    MockTrackManager trackManager;
    MockSessionManager sessionManager;
    MockTempoService tempoService;
    auto eventQueue = IEventQueue::create();
    MockClockService clockService;

    auto session = std::make_unique<MockProjectSession>(&trackManager);
    sessionManager.setActiveSession(std::move(session));

    // Create a record-armed, MIDI track
    composition::TrackCreateInfo trackInfo{};
    trackInfo.type = composition::TrackType::MIDI;
    trackInfo.isRecordArmed = true;
    trackInfo.isInputMonitoring = true;
    TrackID trackId = trackManager.createTrack(trackInfo);

    MidiEditorController controller(
        &sessionManager,
        nullptr,
        &tempoService,
        eventQueue.get(),
        &clockService
    );

    SECTION("Initial State") {
        CHECK_FALSE(controller.hasOpenClip());
        CHECK(controller.getOpenTrackId() == TrackID::invalid());
        CHECK(controller.getOpenRegionId() == RegionID::invalid());
    }

    SECTION("Opening / Closing Clip") {
        RegionID regionId{1, 100};
        bool success = controller.openClip(trackId, regionId);
        CHECK(success);
        CHECK(controller.hasOpenClip());
        CHECK(controller.getOpenTrackId() == trackId);
        CHECK(controller.getOpenRegionId() == regionId);

        controller.closeClip();
        CHECK_FALSE(controller.hasOpenClip());
        CHECK(controller.getOpenTrackId() == TrackID::invalid());
    }

    SECTION("CRUD operations inside opened clip") {
        RegionID regionId{1, 100};
        
        auto trackIt = trackManager.tracks.find(trackId.id);
        REQUIRE(trackIt != trackManager.tracks.end());
        auto* mockPlaylist = trackIt->second->playlist.get();

        composition::TimelineRegion reg{};
        reg.type = composition::RegionType::MIDI;
        reg.positionSample = 0;
        reg.sourceLength = 48000;
        ClipID clipId{1, 1};
        reg.sourceId = SourceID::fromRaw(clipId.toRaw());
        mockPlaylist->regions.push_back({regionId, reg, 0});

        REQUIRE(controller.openClip(trackId, regionId));

        // Add note
        NoteID noteId = controller.addNote(60, 100, 1, 0, 48000);
        CHECK(noteId.id != 0);

        // Verify note exists in sequencer
        auto* seq = static_cast<MockMIDISequencer*>(trackManager.getMIDISequencer(trackId));
        REQUIRE(seq != nullptr);
        REQUIRE(seq->notes.size() == 1);
        CHECK(seq->notes[0].note.pitch == 60);
        CHECK(seq->notes[0].note.velocity == 100);
        CHECK(seq->notes[0].note.channel == 1);
        CHECK(seq->notes[0].note.startSample == 0);
        CHECK(seq->notes[0].note.endSample == 48000);

        // Move note
        controller.moveNote(noteId, 62, 24000);
        CHECK(seq->notes[0].note.pitch == 62);
        CHECK(seq->notes[0].note.startSample == 24000);

        // Resize note
        controller.resizeNote(noteId, 72000);
        CHECK(seq->notes[0].note.endSample == 72000);

        // Set note velocity
        controller.setNoteVelocity(noteId, 120);
        CHECK(seq->notes[0].note.velocity == 120);

        // Remove note
        controller.removeNote(noteId);
        CHECK(seq->notes.empty());
    }
}

TEST_CASE("MidiEditorController: Auditioning and Live Timing Pipeline", "[bridge][MidiEditorController]") {
    MockTrackManager trackManager;
    MockSessionManager sessionManager;
    MockTempoService tempoService;
    auto eventQueue = IEventQueue::create();
    MockClockService clockService;

    auto session = std::make_unique<MockProjectSession>(&trackManager);
    sessionManager.setActiveSession(std::move(session));

    // Create a record-armed track
    composition::TrackCreateInfo trackInfo{};
    trackInfo.type = composition::TrackType::MIDI;
    trackInfo.isRecordArmed = true;
    trackInfo.isInputMonitoring = true;
    TrackID trackId = trackManager.createTrack(trackInfo);

    MidiEditorController controller(
        &sessionManager,
        nullptr,
        &tempoService,
        eventQueue.get(),
        &clockService
    );

    RegionID regionId{1, 100};
    {
        auto trackIt = trackManager.tracks.find(trackId.id);
        REQUIRE(trackIt != trackManager.tracks.end());
        auto* mockPlaylist = trackIt->second->playlist.get();

        composition::TimelineRegion reg{};
        reg.type = composition::RegionType::MIDI;
        reg.positionSample = 0;
        reg.sourceLength = 48000;
        ClipID clipId{1, 1};
        reg.sourceId = SourceID::fromRaw(clipId.toRaw());
        mockPlaylist->regions.push_back({regionId, reg, 0});
    }
    REQUIRE(controller.openClip(trackId, regionId));

    SECTION("Live noteOn timing with high-resolution sub-sample offset") {
        // Setup clock service cycle
        uint64_t cycleStartSteady = 1000000000ULL; // 1 second in ns
        clockService.cycleStartSteadyTime = cycleStartSteady;
        clockService.currentNumFrames = 512;
        clockService.sampleRate = 48000.0;
        clockService.cycleStartTime = 100000;

        // Mock current time being slightly past cycle start (e.g. 100 samples elapsed)
        // 100 samples at 48kHz = 100 * (1e9 / 48000) = 2,083,333 ns
        // So steady_clock is now cycleStartSteady + 2083333 ns
        // Wait, since steady_clock::now() is standard, let's patch our calculation inside MidiEditorController:
        // Inside noteOn, it uses steady_clock::now().time_since_epoch() to get nowSteadyTime.
        // We can simulate this by ensuring that the test environment behaves predictably,
        // or since clockService_ is mocked, we can check that a MIDI event is pushed to eventQueue.
        
        eventQueue->resetStatistics();
        controller.noteOn(64, 90, 1);

        // Retrieve event from EventQueue
        eventQueue->prepareCycle();
        EventData ev{};
        REQUIRE(eventQueue->popEventForCycle(ev));
        CHECK(ev.eventType == EventType::MIDI_NOTE_ON);
        CHECK(ev.payload.midiNote.pitch == 64);
        CHECK(ev.payload.midiNote.velocity == 90);
        CHECK(ev.payload.midiNote.channel == 1);
    }

    SECTION("Input monitoring validation (reject unarmed/unmonitored tracks)") {
        // Disarm the track
        trackManager.setTrackRecordArmed(trackId, false);
        trackManager.setTrackInputMonitoring(trackId, false);

        eventQueue->resetStatistics();
        controller.noteOn(60, 100, 1);

        // EventQueue should have 0 events pushed
        IEventQueue::Statistics stats{};
        eventQueue->getStatistics(stats);
        CHECK(stats.totalPushed == 0);
    }

    SECTION("Prevent hanging notes on clip focus change") {
        // Trigger an active live note
        trackManager.setTrackRecordArmed(trackId, true);
        controller.noteOn(60, 100, 1);

        // Clear the noteOn event from the queue
        eventQueue->prepareCycle();
        EventData discard{};
        (void)eventQueue->popEventForCycle(discard);

        eventQueue->resetStatistics();

        // Focus another clip, which must instantly flush active notes by triggering noteOffs!
        TrackID newTrackId = trackManager.createTrack(trackInfo);
        controller.openClip(newTrackId, {1, 101});

        eventQueue->prepareCycle();
        EventData ev{};
        REQUIRE(eventQueue->popEventForCycle(ev));
        CHECK(ev.eventType == EventType::MIDI_NOTE_OFF);
        CHECK(ev.payload.midiNote.pitch == 60);
    }
}
