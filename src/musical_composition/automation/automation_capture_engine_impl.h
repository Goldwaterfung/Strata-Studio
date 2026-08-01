#pragma once

#include "iautomation_capture_engine.h"
#include "iautomation_lane.h"
#include "Middle Bridge/automation/iautomation_recording_gateway.h"
#include <mutex>
#include <unordered_map>
#include <vector>

namespace composition {

class AutomationCaptureEngineImpl : public IAutomationCaptureEngine {
public:
    AutomationCaptureEngineImpl(
        Layer2::IStringRegistry* stringRegistry,
        Layer3::IAutomationProcessor* processor,
        DSP::ITouchStateMonitor* touchStateMonitor,
        bridge::IAutomationRecordingGateway* recordingGateway
    );
    ~AutomationCaptureEngineImpl() override = default;

    Layer2::SPSCQueue<CapturePoint, 4096>& getCaptureQueue() override { return queue_; }

    // Tick/drain queue
    void process() override;

    // Dynamic session lifecycle
    void setActiveSession(
        ITrackManager* trackManager,
        ICommandHistory* commandHistory
    ) override;

    // Controller actions
    void startRecording(NodeID targetId, uint32_t parameterIndex, AutomationMode mode, uint64_t startSample, float initialValue = 0.0f) override;
    void stopRecording(NodeID targetId, uint32_t parameterIndex, uint64_t stopSample) override;
    void abortRecording(NodeID targetId, uint32_t parameterIndex) override;

    void touchStarted(NodeID targetId, uint32_t parameterIndex) override;
    void touchStopped(NodeID targetId, uint32_t parameterIndex) override;

    // State queries
    bool isRecording(NodeID targetId, uint32_t parameterIndex) const override;
    AutomationMode getMode(NodeID targetId, uint32_t parameterIndex) const override;
    RecorderState getState(NodeID targetId, uint32_t parameterIndex) const override;

    // Data processing
    void thinData(NodeID targetId, uint32_t parameterIndex, float tolerance) override;
    void smoothData(NodeID targetId, uint32_t parameterIndex, uint32_t windowSize) override;

private:
    struct ParameterSession {
        AutomationMode mode = AutomationMode::OFF;
        bool isRecording = false;
        uint64_t startSample = 0;
        float preTouchValue = 0.0f;
        
        std::vector<::AutomationPoint> points;
        
        // Thinning State
        float lastValue = 0.0f;
        uint64_t lastTimestamp = 0;
        bool lastWasRedundant = false;
        float thinningTolerance = 1e-4f;
    };

    uint64_t makeKey(NodeID id, uint32_t index) const {
        return (static_cast<uint64_t>(id.toRaw()) << 32) | index;
    }

    void emitPoint(ParameterSession& session, uint64_t timestamp, float value, NodeID targetId, uint32_t parameterIndex);
    void finalizeSession(ParameterSession& session, uint64_t stopSample, NodeID targetId, uint32_t parameterIndex);
    
    TrackID resolveTrackForNode(NodeID nodeId) const;
    AutomationTarget resolveActiveTarget(NodeID targetId, uint32_t parameterIndex) const;
    void pushUpdatedPointsToProcessor(IAutomationLane* lane, NodeID targetId, uint32_t parameterIndex);

    Layer2::IStringRegistry* stringRegistry_ = nullptr;
    Layer3::IAutomationProcessor* processor_ = nullptr;
    DSP::ITouchStateMonitor* touchStateMonitor_ = nullptr;
    bridge::IAutomationRecordingGateway* recordingGateway_ = nullptr;

    ITrackManager* trackManager_ = nullptr;
    ICommandHistory* commandHistory_ = nullptr;

    Layer2::SPSCQueue<CapturePoint, 4096> queue_;
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, ParameterSession> sessions_;
};

} // namespace composition
