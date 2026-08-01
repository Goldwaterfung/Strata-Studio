#pragma once

#include "common/system_primitives.h"
#include "Core infrastructure/bridges/spsc_queue.h"
#include <memory>

namespace Layer2 { class IStringRegistry; }
namespace Layer3 { class IAutomationProcessor; }
namespace DSP { class ITouchStateMonitor; }
namespace bridge { class IAutomationRecordingGateway; }

namespace composition {

class ITrackManager;
class ICommandHistory;

/**
 * @brief Black box service in Layer 5 managing the capture queue and committing
 *        recorded automation points directly to the project state.
 */
class IAutomationCaptureEngine {
public:
    virtual ~IAutomationCaptureEngine() = default;

    // --- Queue & Tick ---
    virtual Layer2::SPSCQueue<CapturePoint, 4096>& getCaptureQueue() = 0;
    virtual void process() = 0;

    // --- Dynamic Lifecycle ---
    virtual void setActiveSession(
        ITrackManager* trackManager,
        ICommandHistory* commandHistory
    ) = 0;

    // --- Controller Actions (UI forwards high-level gestures) ---
    virtual void startRecording(NodeID targetId, uint32_t parameterIndex, AutomationMode mode, uint64_t startSample, float initialValue = 0.0f) = 0;
    virtual void stopRecording(NodeID targetId, uint32_t parameterIndex, uint64_t stopSample) = 0;
    virtual void abortRecording(NodeID targetId, uint32_t parameterIndex) = 0;

    virtual void touchStarted(NodeID targetId, uint32_t parameterIndex) = 0;
    virtual void touchStopped(NodeID targetId, uint32_t parameterIndex) = 0;

    // --- State Queries ---
    virtual bool isRecording(NodeID targetId, uint32_t parameterIndex) const = 0;
    virtual AutomationMode getMode(NodeID targetId, uint32_t parameterIndex) const = 0;
    virtual RecorderState getState(NodeID targetId, uint32_t parameterIndex) const = 0;

    // --- Data Processing Options ---
    virtual void thinData(NodeID targetId, uint32_t parameterIndex, float tolerance) = 0;
    virtual void smoothData(NodeID targetId, uint32_t parameterIndex, uint32_t windowSize) = 0;

    // --- Factory ---
    static std::unique_ptr<IAutomationCaptureEngine> create(
        Layer2::IStringRegistry* stringRegistry,
        Layer3::IAutomationProcessor* processor,
        DSP::ITouchStateMonitor* touchStateMonitor,
        bridge::IAutomationRecordingGateway* recordingGateway = nullptr
    );
};

} // namespace composition
