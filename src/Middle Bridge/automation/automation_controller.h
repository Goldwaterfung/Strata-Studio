// src/Middle Bridge/automation_controller.h
#pragma once

#include "Middle Bridge/automation/iautomation_controller.h"
#include "project/isession_manager.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "musical_composition/automation/iautomation_lane.h"
#include "Middle Bridge/automation/iautomation_recording_gateway.h"
#include <unordered_map>
#include <map>
#include <vector>

namespace Layer3 {
class ITransport;
class IAutomationProcessor;
}

namespace composition {
class IAutomationCaptureEngine;
}

namespace DSP {
class ITouchStateMonitor;
}

namespace bridge {

class AutomationController : public IAutomationController, public ISessionChangeListener {
public:
    AutomationController(
        ISessionManager* sessionManager,
        Layer2::IStringRegistry* stringRegistry,
        Layer3::ITransport* transport,
        composition::IAutomationCaptureEngine* captureEngine,
        Layer3::IAutomationProcessor* processor,
        DSP::ITouchStateMonitor* touchStateMonitor = nullptr,
        IAutomationRecordingGateway* recordingGateway = nullptr
    );

    ~AutomationController() override;

    // --- Lane selection ---
    void selectActiveAutomationLane(TrackID trackId, NodeID routingNodeId, uint32_t subNodeId, int32_t parameterIndex) override;
    void getActiveAutomationLane(TrackID& outTrackId, NodeID& outTargetNodeId, uint32_t& outSubNodeId, int32_t& outParameterIndex) const override;
    void addAutomationPoint(uint64_t frame, float value) override;
    void removeAutomationPoint(uint32_t pointIndex) override;
    void setPointShapeAndTension(uint32_t pointIndex, uint8_t shape, float tension) override;
    void editPoints(const uint32_t* indices, uint32_t count, int64_t frameDelta, float valueDelta) override;
    void clearAutomationLane() override;
    uint32_t copyAutomationPoints(uint64_t startFrame, uint64_t endFrame) override;
    void pasteAutomationPoints(uint64_t pasteAtFrame) override;
    
    // --- Real-time Recording Mode Setup ---
    void onTransportRecordingStarted() override;
    void onTransportRecordingStopped() override;
    void onTransportStateChanged(bool isPlaying) override;
    void setRecorderMode(AutomationMode mode) override;
    void stopActiveRecording() override;
    void startTouchRecording() override;
    void stopTouchRecording() override;
    void recordValue(float value) override;
    
    // --- Curve Render Queries ---
    float getBaseParameterValue(NodeID targetNodeId, uint32_t subNodeId, uint32_t parameterIndex) const override;
    uint32_t getCurvePoints(
        TrackID trackId,
        NodeID targetNodeId,
        uint32_t subNodeId,
        uint32_t parameterIndex,
        uint64_t startFrame,
        uint64_t endFrame,
        VisualAutomationPoint* outPoints,
        uint32_t maxPoints
    ) const override;

    bool isAutomationVisible(TrackID trackId) const override;
    bool isAutomationWriteEnabled(TrackID trackId) const override;
    void createAutomationLane(TrackID trackId, NodeID routingNodeId, uint32_t subNodeId, uint32_t parameterIndex) override;
    void removeAutomationLane(TrackID trackId, NodeID routingNodeId, uint32_t subNodeId, uint32_t parameterIndex) override;
    std::string queryPluginParameterName(TrackID trackId, NodeID targetNodeId, uint32_t subNodeId, uint32_t parameterIndex) const override;

    // --- ISessionChangeListener ---
    void onSessionChanging() override;
    void onSessionChanged(composition::IProjectSession* session) override;

private:
    composition::IAutomationLane* getActiveLane(bool createIfMissing) const;
    composition::ITrackManager* getTrackManager() const;
    void pushUpdatedPointsToProcessor(composition::IAutomationLane* lane);
    void compileAndPushPoints(const composition::AutomationTarget& target, composition::IAutomationLane* lane);
    uint32_t fetchPointsIntoScratch(composition::IAutomationLane* lane) const;

    /**
     * @brief Builds the AutomationTarget for the currently selected lane from
     *        activeNodeId_, activeParameterIndex_, and the string registry cache.
     *        Called by all four mode-guarded mutation methods.
     */
    composition::AutomationTarget resolveActiveTarget() const;

    ISessionManager* sessionManager_ = nullptr;
    Layer2::IStringRegistry* stringRegistry_ = nullptr;
    Layer3::ITransport* transport_ = nullptr;
    composition::IAutomationCaptureEngine* captureEngine_ = nullptr;
    Layer3::IAutomationProcessor* processor_ = nullptr;
    DSP::ITouchStateMonitor* touchStateMonitor_ = nullptr;
    IAutomationRecordingGateway* recordingGateway_ = nullptr;

    TrackID activeTrackId_{0, 0};
    NodeID activeNodeId_ = NodeID::invalid();
    uint32_t activeTargetSubNodeId_ = 0;
    int32_t activeParameterIndex_ = -1;

    mutable std::map<std::pair<uint64_t, int32_t>, uint32_t> cachedParameterStringIds_;
    mutable std::vector<composition::Point> pointsScratch_;
    std::vector<composition::Point> clipboardPoints_;
};

} // namespace bridge
