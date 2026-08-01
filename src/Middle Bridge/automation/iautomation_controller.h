#pragma once
#include "common/system_primitives.h"
#include "musical_composition/musical_primitives.h"

namespace bridge {

using composition::RegionID;

struct VisualAutomationPoint {
    uint32_t pointIndex;
    uint64_t framePosition;
    float normalizedValue; // Scaled 0.0f to 1.0f
    bool isSelected;
    uint8_t curveShape;
    float tension;
};

/**
 * @brief Interfaces for drawing, adding, and recording automation curve points
 */
class IAutomationController {
public:
    virtual ~IAutomationController() = default;

    // --- Lane selection ---
    virtual void selectActiveAutomationLane(TrackID trackId, NodeID routingNodeId, uint32_t subNodeId, int32_t parameterIndex) = 0;
    virtual void getActiveAutomationLane(TrackID& outTrackId, NodeID& outTargetNodeId, uint32_t& outSubNodeId, int32_t& outParameterIndex) const = 0;
    virtual void addAutomationPoint(uint64_t frame, float value) = 0;
    virtual void removeAutomationPoint(uint32_t pointIndex) = 0;
    virtual void setPointShapeAndTension(uint32_t pointIndex, uint8_t shape, float tension) = 0;
    virtual void editPoints(const uint32_t* indices, uint32_t count, int64_t frameDelta, float valueDelta) = 0;

    virtual void clearAutomationLane() = 0;

    /**
     * @brief Copy automation points from the active lane into an internal clipboard.
     *        If startFrame < endFrame, copies only points within [startFrame, endFrame].
     *        If startFrame == endFrame == 0, copies ALL points from position 0 onward.
     * @return Number of points copied.
     */
    virtual uint32_t copyAutomationPoints(
        uint64_t startFrame,
        uint64_t endFrame
    ) = 0;

    /**
     * @brief Paste previously copied automation points into the active lane.
     *        Points are offset so the earliest copied point lands at pasteAtFrame.
     */
    virtual void pasteAutomationPoints(uint64_t pasteAtFrame) = 0;

    // --- Real-time Recording Mode Setup ---
    virtual void onTransportRecordingStarted() = 0;
    virtual void onTransportRecordingStopped() = 0;
    virtual void onTransportStateChanged(bool isPlaying) = 0;
    virtual void setRecorderMode(AutomationMode mode) = 0;
    virtual void stopActiveRecording() = 0;
    virtual void startTouchRecording() = 0;
    virtual void stopTouchRecording() = 0;
    virtual void recordValue(float value) = 0;
    
    // --- Curve Render Queries ---
    virtual float getBaseParameterValue(NodeID targetNodeId, uint32_t subNodeId, uint32_t parameterIndex) const = 0;
    virtual uint32_t getCurvePoints(
        TrackID trackId,
        NodeID targetNodeId,
        uint32_t subNodeId,
        uint32_t parameterIndex,
        uint64_t startFrame,
        uint64_t endFrame,
        VisualAutomationPoint* outPoints,
        uint32_t maxPoints
    ) const = 0;

    /// Returns true when automation curves should be drawn (all modes except OFF).
    virtual bool isAutomationVisible(TrackID trackId) const = 0;
    virtual std::string queryPluginParameterName(TrackID trackId, NodeID targetNodeId, uint32_t subNodeId, uint32_t parameterIndex) const = 0;

    /// Returns true only for write-enabled modes (WRITE/TOUCH/LATCH/TRIM).
    virtual bool isAutomationWriteEnabled(TrackID trackId) const = 0;

    /**
     * @brief Eagerly create an automation lane for the given parameter on the NRT thread.
     *        Called by ParameterWindow when the user clicks "ADD LANE". Idempotent —
     *        returns the existing lane if it already exists for the same target.
     * @param trackId        The track that owns the lane.
     * @param routingNodeId  The DSP node the parameter belongs to.
     * @param subNodeId      The sub-node ID.
     * @param parameterIndex The parameter index within that node.
     */
    virtual void createAutomationLane(TrackID trackId, NodeID routingNodeId, uint32_t subNodeId, uint32_t parameterIndex) = 0;
    virtual void removeAutomationLane(TrackID trackId, NodeID routingNodeId, uint32_t subNodeId, uint32_t parameterIndex) = 0;
};

} // namespace bridge
