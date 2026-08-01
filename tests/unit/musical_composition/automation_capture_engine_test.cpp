#include <catch2/catch_test_macros.hpp>
#include "musical_composition/automation/iautomation_capture_engine.h"
#include "musical_composition/automation/iautomation_lane_manager.h"
#include "musical_composition/automation/iautomation_lane.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "musical_composition/command_history/icommand_history.h"
#include "Core audio engine/automation/iautomation_processor.h"
#include "Core infrastructure/memory/istring_registry.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <map>
#include "Middle Bridge/automation/iautomation_recording_gateway.h"

using namespace composition;

namespace {

class MockAutomationLane : public IAutomationLane {
public:
    AutomationTarget target;
    std::vector<Point> points;

    MockAutomationLane(const AutomationTarget& t) : target(t) {}

    void addPoint(uint64_t samplePosition, float value) override {
        addPoint(samplePosition, value, Point::Shape::LINEAR, 0.0f);
    }
    void addPoint(uint64_t samplePosition, float value, Point::Shape shape, float tension) override {
        auto it = std::lower_bound(points.begin(), points.end(), samplePosition,
            [](const Point& pt, uint64_t val) { return pt.positionSample < val; });
        if (it != points.end() && it->positionSample == samplePosition) {
            it->value = value;
            it->curveShape = shape;
            it->tension = tension;
        } else {
            points.insert(it, {samplePosition, value, shape, tension});
        }
    }
    void removePoint(uint64_t samplePosition) override {
        points.erase(std::remove_if(points.begin(), points.end(),
            [=](const Point& pt) { return pt.positionSample == samplePosition; }), points.end());
    }
    void clearPoints() override {
        points.clear();
    }
    float evaluate(double frame) const override {
        (void)frame;
        return 0.0f;
    }
    uint32_t getPoints(Point* outPoints, uint32_t maxPoints) const override {
        uint32_t toCopy = std::min(static_cast<uint32_t>(points.size()), maxPoints);
        for (uint32_t i = 0; i < toCopy; ++i) {
            outPoints[i] = points[i];
        }
        return toCopy;
    }
    const AutomationTarget& getTarget() const override {
        return target;
    }
};

class MockAutomationLaneManager : public IAutomationLaneManager {
public:
    AutomationMode mode = AutomationMode::OFF;
    std::unordered_map<uint32_t, std::unique_ptr<MockAutomationLane>> lanes;

    void setAutomationMode(AutomationMode m) override {
        mode = m;
    }
    AutomationMode getAutomationMode() const override {
        return mode;
    }
    IAutomationLane* getLane(const AutomationTarget& target) const override {
        auto it = lanes.find(target.cachedParameterIndex);
        if (it != lanes.end()) return it->second.get();
        return nullptr;
    }
    IAutomationLane* createLane(const AutomationTarget& target, bool pushDelta = false) override {
        (void)pushDelta;
        auto& lane = lanes[target.cachedParameterIndex];
        if (!lane) {
            lane = std::make_unique<MockAutomationLane>(target);
        }
        return lane.get();
    }
    bool addPoint(const AutomationTarget& target, uint64_t samplePosition, float value, ::AutomationPoint::Shape shape = ::AutomationPoint::Shape::LINEAR, float tension = 0.5f) override {
        if (mode == AutomationMode::OFF || mode == AutomationMode::READ) return false;
        auto* lane = createLane(target, false);
        lane->addPoint(samplePosition, value, shape, tension);
        return true;
    }
    bool removePoint(const AutomationTarget& target, uint64_t samplePosition) override {
        if (mode == AutomationMode::OFF || mode == AutomationMode::READ) return false;
        auto* lane = getLane(target);
        if (!lane) return false;
        lane->removePoint(samplePosition);
        return true;
    }
    void removeLane(const AutomationTarget& target, bool pushDelta = false) override {
        (void)pushDelta;
        lanes.erase(target.cachedParameterIndex);
    }
    void renderToEvents(uint64_t, uint32_t, bool, uint64_t, uint64_t, Layer2::IEventQueue*) const override {}
    void editPointShapeAndTension(TrackID, NodeID, uint32_t, uint32_t, uint32_t, uint8_t, float) override {}
};

class MockTrackManager : public ITrackManager {
public:
    std::unordered_map<uint32_t, std::unique_ptr<MockAutomationLaneManager>> managers;
    std::vector<TrackID> tracks;

    TrackID createTrack(const TrackCreateInfo&) override { return {0,0}; }
    void deleteTrack(TrackID) override {}
    void renameTrack(TrackID, uint32_t) override {}
    void setTrackComments(TrackID, uint32_t) override {}
    void setTrackOutputRouting(TrackID, TrackID) override {}
    void moveTrack(TrackID, uint32_t, TrackID) override {}
    void setTrackColor(TrackID, uint32_t) override {}
    void setTrackRecordArmed(TrackID, bool) override {}
    void setTrackInputMonitoring(TrackID, bool) override {}
    void setTrackType(TrackID, TrackType) override {}
    void setTrackTakesExpanded(TrackID, bool) override {}
    void setTrackLocked(TrackID, bool) override {}
    bool isTrackLocked(TrackID) const override { return false; }
    IPlaylist* getPlaylist(TrackID) override { return nullptr; }
    IMIDISequencer* getMIDISequencer(TrackID) override { return nullptr; }
    
    IAutomationLaneManager* getAutomationManager(TrackID id) override {
        auto it = managers.find(id.id);
        if (it != managers.end()) return it->second.get();
        return nullptr;
    }
    
    std::atomic<uint64_t>* getRecordingStartSample(TrackID) override { return nullptr; }

    TrackPipelineDescriptor getPipelineDescriptor(TrackID id) const override {
        TrackPipelineDescriptor desc{};
        desc.trackNode = NodeID{ id.id, 1 };
        return desc;
    }
    NodeID getTrackOutputNode(TrackID) const override { return NodeID::invalid(); }
    std::vector<TrackID> getAllTrackIDs() const override { return tracks; }
    bool getTrackInfo(TrackID, TrackCreateInfo&) const override { return false; }
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
    void registerMixerRoutingCallback(MixerRoutingCallback) override {}
};

class MockStringRegistry : public Layer2::IStringRegistry {
public:
    mutable std::unordered_map<uint32_t, std::string> strings;
    mutable uint32_t nextId = 1;

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

    void unregisterString(uint32_t) override {}
};

class MockAutomationProcessor : public Layer3::IAutomationProcessor {
public:
    uint32_t generateAutomationEvents(uint64_t, uint32_t, EventData*, uint32_t, bool) override { return 0; }
    void recordAutomationValue(NodeID, uint32_t, uint32_t, float, uint64_t) override {}
    void updatePlaybackPoints(NodeID, uint32_t, uint32_t, const ::AutomationPoint*, uint32_t) override {}
};

class MockAutomationRecordingGateway : public bridge::IAutomationRecordingGateway {
public:
    ITrackManager* trackManager_ = nullptr;

    explicit MockAutomationRecordingGateway(ITrackManager* tm) : trackManager_(tm) {}

    void recordValue(TrackID, NodeID, uint32_t, uint64_t, float, ::AutomationPoint::Shape) override {}

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
        auto* lane = manager->createLane(target);
        if (!lane) return;

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

TEST_CASE("AutomationCaptureEngine basic operations", "[Layer5][Automation]") {
    MockStringRegistry stringReg;
    MockAutomationProcessor processor;
    MockTrackManager trackManager;
    MockAutomationRecordingGateway gateway(&trackManager);
    
    auto engine = IAutomationCaptureEngine::create(&stringReg, &processor, nullptr, &gateway);
    auto& queue = engine->getCaptureQueue();
    
    NodeID nodeId{1, 1};
    uint32_t paramIdx = 0;
    
    SECTION("Recording inactive by default") {
        queue.push({nodeId, paramIdx, 100, 0.5f, 0, {0}});
        engine->process();
        
        CHECK(engine->isRecording(nodeId, paramIdx) == false);
        CHECK(engine->getMode(nodeId, paramIdx) == AutomationMode::OFF);
    }
    
    SECTION("Capture process flow") {
        TrackID track1{1, 1};
        trackManager.tracks.push_back(track1);
        
        auto laneMgr = std::make_unique<MockAutomationLaneManager>();
        laneMgr->setAutomationMode(AutomationMode::WRITE);
        trackManager.managers[track1.id] = std::move(laneMgr);
        
        engine->setActiveSession(&trackManager, nullptr);
        
        // Start recording
        engine->startRecording(nodeId, paramIdx, AutomationMode::WRITE, 100, 0.5f);
        CHECK(engine->isRecording(nodeId, paramIdx) == true);
        CHECK(engine->getMode(nodeId, paramIdx) == AutomationMode::WRITE);
        
        // Push thinned points (identical segment)
        queue.push({nodeId, paramIdx, 100, 0.5f, 0, {0}});
        queue.push({nodeId, paramIdx, 200, 0.5f, 0, {0}});
        queue.push({nodeId, paramIdx, 300, 0.5f, 0, {0}});
        
        // Push value change
        queue.push({nodeId, paramIdx, 400, 0.8f, 0, {0}});
        
        engine->process();
        engine->stopRecording(nodeId, paramIdx, 500);
        
        auto* committedMgr = trackManager.getAutomationManager(track1);
        REQUIRE(committedMgr != nullptr);
        
        composition::AutomationTarget target{ nodeId, 1, paramIdx, 0 };
        auto* lane = committedMgr->getLane(target);
        REQUIRE(lane != nullptr);
        
        std::vector<Point> pts(10);
        uint32_t count = lane->getPoints(pts.data(), 10);
        
        // Expected committed points:
        // 1. Point at 100 (0.5f)
        // 2. Point at 300 (0.5f) (end of redundant segment before change)
        // 3. Point at 400 (0.8f) (value change)
        // 4. Point at 500 (0.8f) (stop sample boundary fill)
        REQUIRE(count == 4);
        CHECK(pts[0].positionSample == 100);
        CHECK(pts[0].value == 0.5f);
        CHECK(pts[1].positionSample == 300);
        CHECK(pts[1].value == 0.5f);
        CHECK(pts[2].positionSample == 400);
        CHECK(pts[2].value == 0.8f);
        CHECK(pts[3].positionSample == 500);
        CHECK(pts[3].value == 0.8f);
    }

    SECTION("Boolean parameter capture flow") {
        TrackID track1{1, 1};
        trackManager.tracks.push_back(track1);
        
        auto laneMgr = std::make_unique<MockAutomationLaneManager>();
        laneMgr->setAutomationMode(AutomationMode::WRITE);
        trackManager.managers[track1.id] = std::move(laneMgr);
        
        engine->setActiveSession(&trackManager, nullptr);
        
        uint32_t muteParamIdx = 2; // Mute on channel strip
        
        // Start recording
        engine->startRecording(nodeId, muteParamIdx, AutomationMode::WRITE, 100, 0.0f);
        CHECK(engine->isRecording(nodeId, muteParamIdx) == true);
        
        // Push points
        queue.push({nodeId, muteParamIdx, 100, 0.2f, 0, {0}}); // Should snap to 0.0f
        queue.push({nodeId, muteParamIdx, 200, 0.8f, 0, {0}}); // Should snap to 1.0f
        queue.push({nodeId, muteParamIdx, 300, 0.4f, 0, {0}}); // Should snap to 0.0f
        
        engine->process();
        engine->stopRecording(nodeId, muteParamIdx, 400);
        
        auto* committedMgr = trackManager.getAutomationManager(track1);
        REQUIRE(committedMgr != nullptr);
        
        composition::AutomationTarget target{ nodeId, 1, muteParamIdx, 0 };
        auto* lane = committedMgr->getLane(target);
        REQUIRE(lane != nullptr);
        
        std::vector<Point> pts(10);
        uint32_t count = lane->getPoints(pts.data(), 10);
        
        // Expected committed points:
        // 1. Point at 100 (0.0f)
        // 2. Point at 200 (1.0f)
        // 3. Point at 300 (0.0f)
        // 4. Point at 400 (0.0f) (stop sample boundary fill)
        REQUIRE(count == 4);
        CHECK(pts[0].positionSample == 100);
        CHECK(pts[0].value == 0.0f);
        CHECK(pts[0].curveShape == ::AutomationPoint::Shape::STEP);
        
        CHECK(pts[1].positionSample == 200);
        CHECK(pts[1].value == 1.0f);
        CHECK(pts[1].curveShape == ::AutomationPoint::Shape::STEP);
        
        CHECK(pts[2].positionSample == 300);
        CHECK(pts[2].value == 0.0f);
        CHECK(pts[2].curveShape == ::AutomationPoint::Shape::STEP);
        
        CHECK(pts[3].positionSample == 400);
        CHECK(pts[3].value == 0.0f);
        CHECK(pts[3].curveShape == ::AutomationPoint::Shape::STEP);
    }
}
