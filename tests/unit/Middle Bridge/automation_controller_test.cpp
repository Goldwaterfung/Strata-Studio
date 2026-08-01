// tests/unit/Middle Bridge/automation_controller_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Middle Bridge/automation/automation_controller.h"
#include "musical_composition/automation/iautomation_lane_manager.h"
#include "musical_composition/automation/iautomation_lane.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "Core audio engine/transport/itransport.h"
#include "Core audio engine/automation/iautomation_processor.h"
#include "musical_composition/automation/iautomation_capture_engine.h"
#include "common/dsp/curve_interpolation.h"
#include "musical_composition/automation/automation_lane_manager_impl.h"
#include "musical_composition/automation/automation_lane_impl.h"
#include "musical_composition/playlist/iplaylist.h"
#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/interfaces/iregion_metadata_manager.h"
#include <iostream>
#include <memory>
#include <unordered_map>
#include <algorithm>
#include <vector>
#include <map>

using namespace bridge;
using namespace composition;
using namespace Layer2;

namespace Layer2 { class IEventQueue; }
namespace Layer3 { class IPluginManager; }
#include "Middle Bridge/automation/iautomation_recording_gateway.h"

namespace {

class MockPlaylist : public composition::IPlaylist {
public:
    std::vector<composition::IPlaylist::RegionInfo> regions;
    uint32_t nextRegionId = 1;

    composition::RegionID addRegion(const composition::TimelineRegion& region, composition::IPlaylist::LayerIndex layer) override {
        composition::RegionID id;
        id.id = nextRegionId++;
        id.generation = 1;
        
        composition::IPlaylist::RegionInfo info{ id, region, layer == composition::IPlaylist::AUTO_LAYER ? static_cast<composition::IPlaylist::LayerIndex>(0) : layer };
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
};

class MockTrackManager : public ITrackManager {
public:
    std::unordered_map<uint32_t, std::unique_ptr<composition::IAutomationLaneManager>> automationManagers;
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
    
    IPlaylist* getPlaylist(TrackID /*id*/) override { return nullptr; }
    IMIDISequencer* getMIDISequencer(TrackID /*id*/) override { return nullptr; }
    
    IAutomationLaneManager* getAutomationManager(TrackID id) override {
        auto it = automationManagers.find(id.id);
        if (it != automationManagers.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    std::atomic<uint64_t>* getRecordingStartSample(TrackID) override { return nullptr; }

    TrackPipelineDescriptor getPipelineDescriptor(TrackID /*id*/) const override { return {}; }
    NodeID getTrackOutputNode(TrackID /*id*/) const override { return NodeID::invalid(); }
    
    std::vector<TrackID> getAllTrackIDs() const override {
        return allTrackIds;
    }

    bool getTrackInfo(TrackID /*id*/, TrackCreateInfo& /*outInfo*/) const override { return false; }
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
    void getRegionMetadata(RegionID, composition::RegionMetadata&) const override {}
    bool hasRegionMetadata(RegionID) const override { return false; }
    void setRegionMetadata(RegionID, const composition::RegionMetadata&, bool) override {}
    void removeRegionMetadata(RegionID, bool) override {}
    void clear() override {}
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

class MockTransport : public Layer3::ITransport {
public:
    uint64_t position = 0;
    TransportState state = TransportState::STOPPED;

    void play() override { state = TransportState::PLAYING; }
    void stop() override { state = TransportState::STOPPED; }
    bool record() override { state = TransportState::RECORDING; return true; }
    void setState(TransportState s) override { state = s; }
    TransportState getState() const override { return state; }
    void seek(uint64_t pos, SeekMode mode) override { (void)mode; position = pos; }
    uint64_t getPosition() const override { return position; }
    TransportPosition getDetailedPosition() const override { return {}; }
    bool advancePosition(uint32_t numSamples) override { (void)numSamples; return false; }
    void setLoopRange(uint64_t start, uint64_t end) override { (void)start; (void)end; }
    void setLoopEnabled(bool enabled) override { (void)enabled; }
    LoopState getLoopState() const override { return {}; }
    void setMetronomeEnabled(bool enabled) override { (void)enabled; }
    bool isMetronomeEnabled() const override { return false; }
    void setTempoService(Layer2::ITempoService* ts) override { (void)ts; }
    void setStateManager(Layer2::IStateManager* sm) override { (void)sm; }
    void updateTempoCache() override {}
    uint64_t createTransportSnapshot() const override { return 0; }
    bool restoreTransportSnapshot(uint64_t snapshotId) override { (void)snapshotId; return false; }
    double samplesToBeats(uint64_t samples) const override { (void)samples; return 0.0; }
    uint64_t beatsToSamples(double beats) const override { (void)beats; return 0; }
    bool isRecordArmed() const override { return false; }
    void setRecordArmed(bool armed) override { (void)armed; }
};

class MockAutomationCaptureEngine : public composition::IAutomationCaptureEngine {
public:
    Layer2::SPSCQueue<CapturePoint, 4096> queue;
    bool recording = false;
    AutomationMode recordMode = AutomationMode::OFF;
    uint64_t startSample = 0;
    std::vector<::AutomationPoint> recordedPoints;
    composition::ITrackManager* trackManager = nullptr;
    composition::ICommandHistory* commandHistory = nullptr;

    Layer2::SPSCQueue<CapturePoint, 4096>& getCaptureQueue() override { return queue; }
    void process() override {}
    void setActiveSession(composition::ITrackManager* tm, composition::ICommandHistory* ch) override {
        trackManager = tm;
        commandHistory = ch;
    }
    void startRecording(NodeID targetId, uint32_t parameterIndex, AutomationMode mode, uint64_t start, float initialValue = 0.0f) override {
        (void)targetId;
        (void)parameterIndex;
        (void)initialValue;
        recording = true;
        recordMode = mode;
        startSample = start;
    }
    void stopRecording(NodeID targetId, uint32_t parameterIndex, uint64_t stopSample) override {
        (void)targetId;
        (void)parameterIndex;
        (void)stopSample;
        recording = false;
        std::cout << "[DEBUG] stopRecording: targetId=" << targetId.id << ", param=" << parameterIndex
                  << ", stopSample=" << stopSample << ", startSample=" << startSample 
                  << ", mode=" << static_cast<int>(recordMode) << std::endl;
        if (!recordedPoints.empty() && trackManager) {
            TrackID track1{1, 1};
            auto* laneManager = trackManager->getAutomationManager(track1);
            if (laneManager) {
                composition::AutomationTarget target{ targetId, 1, parameterIndex };
                auto* lane = laneManager->createLane(target);
                if (lane) {
                    std::vector<::AutomationPoint> existingPoints(128);
                    uint32_t count = lane->getPoints(existingPoints.data(), 128);
                    existingPoints.resize(count);
                    std::cout << "[DEBUG] lane resolved. existingPoints size=" << count << std::endl;
                    
                    if (recordMode == AutomationMode::WRITE) {
                        for (const auto& pt : existingPoints) {
                            if (pt.positionSample >= startSample && pt.positionSample <= stopSample) {
                                std::cout << "[DEBUG] removing point at " << pt.positionSample << std::endl;
                                lane->removePoint(pt.positionSample);
                            }
                        }
                        for (const auto& pt : recordedPoints) {
                            std::cout << "[DEBUG] adding recorded point at " << pt.positionSample << ", val=" << pt.value << std::endl;
                            lane->addPoint(pt.positionSample, pt.value, pt.curveShape, pt.tension);
                        }
                    }
                    else if (recordMode == AutomationMode::TRIM) {
                        float preTouchValue = 0.0f;
                        std::map<uint64_t, ::AutomationPoint> merged;
                        
                        auto getRecordedValueAtLocal = [&](uint64_t ts) -> float {
                            if (recordedPoints.empty()) return preTouchValue;
                            if (ts <= recordedPoints.front().positionSample) return recordedPoints.front().value;
                            if (ts >= recordedPoints.back().positionSample) return recordedPoints.back().value;
                            for (size_t i = 0; i < recordedPoints.size() - 1; ++i) {
                                    if (ts >= recordedPoints[i].positionSample && ts <= recordedPoints[i+1].positionSample) {
                                        return DSP::CurveInterpolator::calculate(recordedPoints[i], recordedPoints[i+1], ts);
                                    }
                            }
                            return preTouchValue;
                        };
                        
                        auto getExistingValueAtLocal = [&](uint64_t ts) -> float {
                            if (existingPoints.empty()) return 0.0f;
                            if (ts <= existingPoints.front().positionSample) return existingPoints.front().value;
                            if (ts >= existingPoints.back().positionSample) return existingPoints.back().value;
                            for (size_t i = 0; i < existingPoints.size() - 1; ++i) {
                                    if (ts >= existingPoints[i].positionSample && ts <= existingPoints[i+1].positionSample) {
                                        return DSP::CurveInterpolator::calculate(existingPoints[i], existingPoints[i+1], ts);
                                    }
                            }
                            return 0.0f;
                        };
                        
                        uint64_t rangeStart = recordedPoints.front().positionSample;
                        uint64_t rangeEnd   = recordedPoints.back().positionSample;
                        
                        for (const auto& pt : existingPoints) {
                            if (pt.positionSample >= rangeStart && pt.positionSample <= rangeEnd) {
                                float delta = getRecordedValueAtLocal(pt.positionSample) - preTouchValue;
                                float newVal = std::clamp(pt.value + delta, 0.0f, 1.0f);
                                merged[pt.positionSample] = { pt.positionSample, newVal, pt.curveShape, pt.tension };
                            }
                        }
                        for (const auto& pt : recordedPoints) {
                            float existingVal = getExistingValueAtLocal(pt.positionSample);
                            float delta = pt.value - preTouchValue;
                            float newVal = std::clamp(existingVal + delta, 0.0f, 1.0f);
                            merged[pt.positionSample] = { pt.positionSample, newVal, pt.curveShape, pt.tension };
                        }
                        for (const auto& pt : existingPoints) {
                            if (pt.positionSample >= rangeStart && pt.positionSample <= rangeEnd) {
                                lane->removePoint(pt.positionSample);
                            }
                        }
                        for (const auto& [pos, mergedPt] : merged) {
                            lane->addPoint(mergedPt.positionSample, mergedPt.value, mergedPt.curveShape, mergedPt.tension);
                        }
                    }
                }
            }
        }
    }
    void abortRecording(NodeID targetId, uint32_t parameterIndex) override {
        (void)targetId;
        (void)parameterIndex;
        recording = false;
    }
    void touchStarted(NodeID targetId, uint32_t parameterIndex) override {
        (void)targetId;
        (void)parameterIndex;
    }
    void touchStopped(NodeID targetId, uint32_t parameterIndex) override {
        (void)targetId;
        (void)parameterIndex;
    }
    bool isRecording(NodeID targetId, uint32_t parameterIndex) const override {
        (void)targetId;
        (void)parameterIndex;
        return recording;
    }
    AutomationMode getMode(NodeID targetId, uint32_t parameterIndex) const override {
        (void)targetId;
        (void)parameterIndex;
        return recordMode;
    }
    RecorderState getState(NodeID targetId, uint32_t parameterIndex) const override {
        return { targetId, parameterIndex, recordMode, recording, startSample };
    }
    void thinData(NodeID targetId, uint32_t parameterIndex, float tolerance) override {
        (void)targetId;
        (void)parameterIndex;
        (void)tolerance;
    }
    void smoothData(NodeID targetId, uint32_t parameterIndex, uint32_t windowSize) override {
        (void)targetId;
        (void)parameterIndex;
        (void)windowSize;
    }
};

class MockAutomationProcessor : public Layer3::IAutomationProcessor {
public:
    struct LanePoints {
        std::vector<::AutomationPoint> points;
    };
    std::unordered_map<uint32_t, LanePoints> lanes;

    uint32_t generateAutomationEvents(uint64_t startPosition, uint32_t numSamples, EventData* outEvents, uint32_t maxEvents, bool isPlaying) override {
        (void)startPosition;
        (void)numSamples;
        (void)outEvents;
        (void)maxEvents;
        (void)isPlaying;
        return 0;
    }
    void recordAutomationValue(NodeID targetNodeId, uint32_t subNodeId, uint32_t parameterIndex, float value, uint64_t position) override {
        (void)targetNodeId;
        (void)subNodeId;
        (void)parameterIndex;
        (void)value;
        (void)position;
    }
    void updatePlaybackPoints(NodeID targetNodeId, uint32_t subNodeId, uint32_t parameterIndex, const ::AutomationPoint* points, uint32_t count) override {
        (void)subNodeId;
        // Use an arbitrary key combining node ID and param for the mock map
        auto& lane = lanes[targetNodeId.id + parameterIndex * 10000];
        lane.points.clear();
        for (uint32_t i = 0; i < count; ++i) {
            lane.points.push_back(points[i]);
        }
    }
};

class MockAutomationRecordingGateway : public bridge::IAutomationRecordingGateway {
public:
    composition::ITrackManager* trackManager_ = nullptr;
    
    explicit MockAutomationRecordingGateway(composition::ITrackManager* tm) : trackManager_(tm) {}

    void recordValue(TrackID trackId, NodeID targetNodeId, uint32_t parameterIndex, uint64_t samplePosition, float value, ::AutomationPoint::Shape shape) override {
        if (!trackManager_) return;
        auto* manager = trackManager_->getAutomationManager(trackId);
        if (!manager) return;
        composition::AutomationTarget target{ targetNodeId, 1, parameterIndex, 0 };
        manager->addPoint(target, samplePosition, value, shape, 0.5f);
    }

    void commitBatch(
        TrackID trackId,
        NodeID targetNodeId,
        uint32_t parameterIndex,
        const std::vector<::AutomationPoint>& points,
        uint64_t startSample,
        uint64_t stopSample,
        AutomationMode /*mode*/
    ) override {
        if (!trackManager_) return;
        auto* manager = trackManager_->getAutomationManager(trackId);
        if (!manager) return;

        composition::AutomationTarget target{ targetNodeId, 1, parameterIndex, 0 };
        auto* lane = manager->getLane(target);
        if (!lane) {
            lane = manager->createLane(target);
        }

        std::vector<::AutomationPoint> existing(4096);
        uint32_t cnt = lane->getPoints(existing.data(), static_cast<uint32_t>(existing.size()));
        existing.resize(cnt);

        for (const auto& pt : existing) {
            if (pt.positionSample >= startSample && pt.positionSample <= stopSample) {
                lane->removePoint(pt.positionSample);
            }
        }
        for (const auto& pt : points) {
            lane->addPoint(pt.positionSample, pt.value, pt.curveShape, pt.tension);
        }
    }
};

} // namespace

TEST_CASE("AutomationController: Points CRUD and Selection", "[bridge][AutomationController]") {
    MockTrackManager trackManager;
    MockStringRegistry stringRegistry;
    MockSessionManager sessionManager;

    TrackID track1{1, 1};
    trackManager.allTrackIds.push_back(track1);
    trackManager.automationManagers[track1.id] = std::make_unique<composition::AutomationLaneManagerImpl>(track1, nullptr);

    MockTransport transport;
    MockAutomationCaptureEngine recorder;
    MockAutomationProcessor processor;
    MockAutomationRecordingGateway gateway(&trackManager);

    AutomationController controller(&sessionManager, &stringRegistry, &transport, &recorder, &processor, nullptr, &gateway);

    auto session = std::make_unique<MockProjectSession>(&trackManager);
    sessionManager.setActiveSession(std::move(session));

    NodeID dspNode{2, 1};
    uint32_t volumeParam = 0; // Volume

    controller.selectActiveAutomationLane(track1, dspNode, 0, static_cast<int32_t>(volumeParam));

    SECTION("Active lane query") {
        TrackID actTrack = TrackID::invalid();
        NodeID actNode = NodeID::invalid();
        uint32_t actSubNode = 0;
        int32_t actParam = -1;
        controller.getActiveAutomationLane(actTrack, actNode, actSubNode, actParam);
        CHECK(actTrack == track1);
        CHECK(actNode == dspNode);
        CHECK(actParam == static_cast<int32_t>(volumeParam));
    }

    SECTION("Add and query points") {
        auto* manager = trackManager.automationManagers[track1.id].get();
        manager->setAutomationMode(AutomationMode::WRITE); // guard requires write mode
        controller.addAutomationPoint(1000, 0.2f);
        controller.addAutomationPoint(5000, 0.8f);

        composition::AutomationTarget target{ dspNode, stringRegistry.registerString("Param_0"), volumeParam };
        auto* lane = manager->getLane(target);
        REQUIRE(lane != nullptr);

        std::vector<composition::Point> pts(10);
        uint32_t ptCount = lane->getPoints(pts.data(), 10);
        REQUIRE(ptCount == 2);
        CHECK(pts[0].positionSample == 1000);
        CHECK(pts[0].value == 0.2f);
        CHECK(pts[1].positionSample == 5000);
        CHECK(pts[1].value == 0.8f);

        // Viewport culling query
        VisualAutomationPoint vPoints[10];
        uint32_t count = controller.getCurvePoints(track1, dspNode, 0, volumeParam, 0, 3000, vPoints, 10);
        REQUIRE(count == 2);
        CHECK(vPoints[0].framePosition == 1000);
        CHECK(vPoints[0].normalizedValue == 0.2f);
        CHECK(vPoints[1].framePosition == 5000);
        CHECK(vPoints[1].normalizedValue == 0.8f);
    }

    SECTION("Remove point by index") {
        auto* manager = trackManager.automationManagers[track1.id].get();
        manager->setAutomationMode(AutomationMode::WRITE); // guard requires write mode
        controller.addAutomationPoint(2000, 0.5f);
        controller.addAutomationPoint(4000, 0.9f);

        composition::AutomationTarget target{ dspNode, stringRegistry.registerString("Param_0"), volumeParam };
        auto* lane = manager->getLane(target);

        std::vector<composition::Point> pts(10);
        uint32_t ptCount = lane->getPoints(pts.data(), 10);
        REQUIRE(ptCount == 2);

        // Remove point at index 0 (which is position 2000)
        controller.removeAutomationPoint(0);
        ptCount = lane->getPoints(pts.data(), 10);
        REQUIRE(ptCount == 1);
        CHECK(pts[0].positionSample == 4000);
    }

    SECTION("Batch Edit Points") {
        auto* manager = trackManager.automationManagers[track1.id].get();
        manager->setAutomationMode(AutomationMode::WRITE); // guard requires write mode
        controller.addAutomationPoint(1000, 0.1f);
        controller.addAutomationPoint(2000, 0.2f);
        controller.addAutomationPoint(3000, 0.3f);

        composition::AutomationTarget target{ dspNode, stringRegistry.registerString("Param_0"), volumeParam };
        auto* lane = manager->getLane(target);

        // Shift points at indices 0 and 2 (positions 1000 and 3000) by +500 frames and +0.05f value
        uint32_t editIndices[] = {0, 2};
        controller.editPoints(editIndices, 2, 500, 0.05f);

        std::vector<composition::Point> pts(10);
        uint32_t ptCount = lane->getPoints(pts.data(), 10);
        REQUIRE(ptCount == 3);
        CHECK(pts[0].positionSample == 1500);
        CHECK(pts[0].value == Catch::Approx(0.15f));
        CHECK(pts[1].positionSample == 2000);
        CHECK(pts[1].value == Catch::Approx(0.2f));
        CHECK(pts[2].positionSample == 3500);
        CHECK(pts[2].value == Catch::Approx(0.35f));
    }

    SECTION("Add and query points for boolean parameter") {
        uint32_t muteParam = 2; // Mute
        controller.selectActiveAutomationLane(track1, dspNode, 0, static_cast<int32_t>(muteParam));

        auto* manager = trackManager.automationManagers[track1.id].get();
        manager->setAutomationMode(AutomationMode::WRITE); // guard requires write mode
        controller.addAutomationPoint(1000, 0.2f); // snaps to 0.0f
        controller.addAutomationPoint(5000, 0.8f); // snaps to 1.0f

        composition::AutomationTarget target{ dspNode, stringRegistry.registerString("Param_2"), muteParam };
        auto* lane = manager->getLane(target);
        REQUIRE(lane != nullptr);

        std::vector<composition::Point> pts(10);
        uint32_t ptCount = lane->getPoints(pts.data(), 10);
        REQUIRE(ptCount == 2);
        CHECK(pts[0].positionSample == 1000);
        CHECK(pts[0].value == 0.0f);
        CHECK(pts[0].curveShape == ::AutomationPoint::Shape::STEP);
        CHECK(pts[1].positionSample == 5000);
        CHECK(pts[1].value == 1.0f);
        CHECK(pts[1].curveShape == ::AutomationPoint::Shape::STEP);
    }

    SECTION("Batch Edit Points for boolean parameter") {
        uint32_t muteParam = 2; // Mute
        controller.selectActiveAutomationLane(track1, dspNode, 0, static_cast<int32_t>(muteParam));

        auto* manager = trackManager.automationManagers[track1.id].get();
        manager->setAutomationMode(AutomationMode::WRITE); // guard requires write mode
        controller.addAutomationPoint(1000, 0.0f);
        controller.addAutomationPoint(2000, 1.0f);
        controller.addAutomationPoint(3000, 0.0f);

        composition::AutomationTarget target{ dspNode, stringRegistry.registerString("Param_2"), muteParam };
        auto* lane = manager->getLane(target);

        // Shift points and check snap: value delta is +0.6f
        uint32_t editIndices[] = {0, 2};
        controller.editPoints(editIndices, 2, 500, 0.6f); // 0.0f + 0.6f = 0.6f -> snaps to 1.0f

        std::vector<composition::Point> pts(10);
        uint32_t ptCount = lane->getPoints(pts.data(), 10);
        REQUIRE(ptCount == 3);
        CHECK(pts[0].positionSample == 1500);
        CHECK(pts[0].value == 1.0f);
        CHECK(pts[0].curveShape == ::AutomationPoint::Shape::STEP);
        CHECK(pts[1].positionSample == 2000);
        CHECK(pts[1].value == 1.0f);
        CHECK(pts[1].curveShape == ::AutomationPoint::Shape::STEP);
        CHECK(pts[2].positionSample == 3500);
        CHECK(pts[2].value == 1.0f);
        CHECK(pts[2].curveShape == ::AutomationPoint::Shape::STEP);
    }
}

TEST_CASE("AutomationController: Recorder Mode Setup", "[bridge][AutomationController]") {
    MockTrackManager trackManager;
    MockStringRegistry stringRegistry;
    MockSessionManager sessionManager;

    TrackID track1{1, 1};
    trackManager.allTrackIds.push_back(track1);
    trackManager.automationManagers[track1.id] = std::make_unique<composition::AutomationLaneManagerImpl>(track1, nullptr);

    MockTransport transport;
    MockAutomationCaptureEngine recorder;
    MockAutomationProcessor processor;
    MockAutomationRecordingGateway gateway(&trackManager);

    AutomationController controller(&sessionManager, &stringRegistry, &transport, &recorder, &processor, nullptr, &gateway);

    auto session = std::make_unique<MockProjectSession>(&trackManager);
    sessionManager.setActiveSession(std::move(session));

    controller.selectActiveAutomationLane(track1, NodeID{3, 1}, 0, 1);

    SECTION("Set recorder mode") {
        auto* manager = trackManager.automationManagers[track1.id].get();
        
        controller.setRecorderMode(AutomationMode::WRITE);
        CHECK(manager->getAutomationMode() == AutomationMode::WRITE);

        controller.setRecorderMode(AutomationMode::TOUCH);
        CHECK(manager->getAutomationMode() == AutomationMode::TOUCH);

        controller.setRecorderMode(AutomationMode::OFF);
        CHECK(manager->getAutomationMode() == AutomationMode::OFF);
    }
}

TEST_CASE("AutomationController: Session Finalization and Merging Math", "[bridge][AutomationController]") {
    MockTrackManager trackManager;
    MockStringRegistry stringRegistry;
    MockSessionManager sessionManager;

    TrackID track1{1, 1};
    trackManager.allTrackIds.push_back(track1);
    trackManager.automationManagers[track1.id] = std::make_unique<composition::AutomationLaneManagerImpl>(track1, nullptr);

    MockTransport transport;
    MockAutomationCaptureEngine recorder;
    MockAutomationProcessor processor;
    MockAutomationRecordingGateway gateway(&trackManager);

    AutomationController controller(&sessionManager, &stringRegistry, &transport, &recorder, &processor, nullptr, &gateway);

    auto session = std::make_unique<MockProjectSession>(&trackManager);
    sessionManager.setActiveSession(std::move(session));

    NodeID dspNode{2, 1};
    uint32_t volumeParam = 0; // Volume

    controller.selectActiveAutomationLane(track1, dspNode, 0, static_cast<int32_t>(volumeParam));

    SECTION("WRITE Mode Overwrite") {
        // Pre-populate lane with some points (mode guard requires WRITE)
        auto* manager0 = trackManager.automationManagers[track1.id].get();
        manager0->setAutomationMode(AutomationMode::WRITE);
        controller.addAutomationPoint(1000, 0.5f);
        controller.addAutomationPoint(2000, 0.6f);
        controller.addAutomationPoint(3000, 0.7f);
        manager0->setAutomationMode(AutomationMode::READ); // reset before recording setup

        // Configure recorder and transport
        transport.position = 2500; // stop position
        recorder.startRecording(dspNode, volumeParam, AutomationMode::WRITE, 1500);
        
        // Add some recorded points
        recorder.recordedPoints = {
            { 1600, 0.1f, ::AutomationPoint::Shape::LINEAR, 0.5f },
            { 2400, 0.2f, ::AutomationPoint::Shape::LINEAR, 0.5f }
        };

        // Stop recording
        controller.stopActiveRecording();

        auto* manager = trackManager.automationManagers[track1.id].get();
        composition::AutomationTarget target{ dspNode, stringRegistry.registerString("Param_0"), volumeParam };
        auto* lane = manager->getLane(target);
        REQUIRE(lane != nullptr);

        std::vector<composition::Point> pts(10);
        uint32_t ptCount = lane->getPoints(pts.data(), 10);
        REQUIRE(ptCount == 4);
        CHECK(pts[0].positionSample == 1000);
        CHECK(pts[0].value == 0.5f);
        CHECK(pts[1].positionSample == 1600);
        CHECK(pts[1].value == 0.1f);
        CHECK(pts[2].positionSample == 2400);
        CHECK(pts[2].value == 0.2f);
        CHECK(pts[3].positionSample == 3000);
        CHECK(pts[3].value == 0.7f);
    }

    SECTION("TRIM Mode Merging Delta") {
        // Pre-populate lane: linear ramp from 0.0f at 1000 to 1.0f at 3000.
        // At 2000, value should be 0.5f.
        auto* manager0 = trackManager.automationManagers[track1.id].get();
        manager0->setAutomationMode(AutomationMode::WRITE);
        controller.addAutomationPoint(1000, 0.0f);
        controller.addAutomationPoint(3000, 1.0f);
        manager0->setAutomationMode(AutomationMode::READ);

        // Configure recorder and transport.
        // In TRIM mode, recorded values are target values relative to start.
        // Let's set start position at 1000, where the curve has value 0.0f.
        // At 2000, recorded value is 0.2f, meaning a delta of +0.2f relative to the value at start.
        transport.position = 3000;
        recorder.startRecording(dspNode, volumeParam, AutomationMode::TRIM, 1000);
        
        recorder.recordedPoints = {
            { 1000, 0.0f, ::AutomationPoint::Shape::LINEAR, 0.5f },
            { 2000, 0.2f, ::AutomationPoint::Shape::LINEAR, 0.5f },
            { 3000, 0.1f, ::AutomationPoint::Shape::LINEAR, 0.5f }
        };

        controller.stopActiveRecording();

        auto* manager = trackManager.automationManagers[track1.id].get();
        composition::AutomationTarget target{ dspNode, stringRegistry.registerString("Param_0"), volumeParam };
        auto* lane = manager->getLane(target);
        REQUIRE(lane != nullptr);

        std::vector<composition::Point> pts(10);
        uint32_t ptCount = lane->getPoints(pts.data(), 10);
        REQUIRE(ptCount == 3);
        CHECK(pts[0].positionSample == 1000);
        CHECK(pts[0].value == 0.0f);
        CHECK(pts[1].positionSample == 2000);
        CHECK(pts[1].value == Catch::Approx(0.7f));
        CHECK(pts[2].positionSample == 3000);
        CHECK(pts[2].value == Catch::Approx(1.0f));
    }
}
