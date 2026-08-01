#pragma once
#include "common/system_primitives.h"
#include <vector>

namespace bridge { class ISessionManager; }
namespace Layer2 { class IStringRegistry; }
namespace Layer3 { class IAutomationProcessor; }

namespace bridge {

class IAutomationRecordingGateway {
public:
    virtual ~IAutomationRecordingGateway() = default;

    /// Record a single automation value and guarantee NRT lane + RT processor are consistent.
    /// Creates the lane if it doesn't exist. Respects the AutomationLaneManager's mode guard
    /// (no-op if mode == OFF or READ).
    /// @param trackId        Owning track
    /// @param targetNodeId   DSP node being automated (e.g. SendNode, PannerNode, ChannelStripNode)
    /// @param parameterIndex Parameter index on that node (0 = gain/volume for most)
    /// @param samplePosition Timeline position in samples
    /// @param value          Normalized value [0, 1]
    /// @param shape          Curve shape (LINEAR for smooth params, STEP for booleans)
    virtual void recordValue(
        TrackID trackId,
        NodeID targetNodeId,
        uint32_t parameterIndex,
        uint64_t samplePosition,
        float value,
        ::AutomationPoint::Shape shape
    ) = 0;

    /// Batch-commit a set of recorded points (used by AutomationCaptureEngine).
    /// Replaces existing points in [startSample, stopSample] per the given mode (WRITE, TOUCH etc.)
    /// Then pushes the full lane to the RT processor.
    virtual void commitBatch(
        TrackID trackId,
        NodeID targetNodeId,
        uint32_t parameterIndex,
        const std::vector<::AutomationPoint>& points,
        uint64_t startSample,
        uint64_t stopSample,
        AutomationMode mode
    ) = 0;

    static std::unique_ptr<IAutomationRecordingGateway> create(
        ISessionManager* sessionManager,
        Layer2::IStringRegistry* stringRegistry,
        Layer3::IAutomationProcessor* processor
    );
};

} // namespace bridge
