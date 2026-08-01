#pragma once
#include "telemetry/imetering_provider.h"
#include "Core infrastructure/bridges/itelemetry_bridge.h"
#include <unordered_map>
#include <mutex>

namespace bridge {

class ISessionManager;

/**
 * @brief Concrete implementation of the Unified Metering & Telemetry Facade.
 */
class MeteringProvider : public IMeteringProvider {
public:
    explicit MeteringProvider(Layer2::ITelemetryBridge* telemetryBridge, ISessionManager* sessionManager = nullptr);
    ~MeteringProvider() override;

    // --- IMeteringProvider Interface ---
    MeterLevel getTrackLevels(TrackID id) override;
    void resetTrackClip(TrackID id) override;

    MeterLevel getMasterLevels() override;
    void resetMasterClip() override;

    void registerTrackNodeMapping(TrackID trackId, NodeID nodeId) override;
    void unregisterTrackNodeMapping(TrackID trackId) override;

    /// Register the master analysis node so its telemetry frames are routed
    /// to the master meter state rather than being silently dropped.
    void registerMasterAnalysisNode(NodeID masterAnalysisNodeId);

    void updateMeters(double elapsedMilliseconds) override;

    void getSpectrumData(NodeID analyzerNodeId, float* outMagnitudes, uint32_t binCount) override;

private:
    struct TrackMeterState {
        // Raw values polled from telemetry in dB
        float targetPeakLeft = -120.0f;
        float targetPeakRight = -120.0f;
        float targetRmsLeft = -120.0f;
        float targetRmsRight = -120.0f;

        // Current smoothed values in dB
        float currentPeakLeft = -120.0f;
        float currentPeakRight = -120.0f;
        float currentRmsLeft = -120.0f;
        float currentRmsRight = -120.0f;

        // Ballistics filters
        BallisticsFilter peakLeftFilter;
        BallisticsFilter peakRightFilter;
        BallisticsFilter rmsLeftFilter;
        BallisticsFilter rmsRightFilter;

        // Sticky clipping indicators
        bool clipLeft = false;
        bool clipRight = false;

        // Track channel format layout count
        uint32_t channelCount = 2; // Default to stereo

        // Time since last telemetry frame was received for this track
        double timeSinceLastFrameMs = 0.0;

        // Sequence counter of the last received telemetry frame
        uint64_t lastSequence = 0;
    };

    Layer2::ITelemetryBridge* telemetryBridge_;
    ISessionManager* sessionManager_ = nullptr;

    // Thread safety for NRT UI / Timer access
    std::mutex mutex_;

    // Maps TrackID (Layer 5) to its corresponding Analysis/Fader NodeID (Layer 4)
    std::unordered_map<uint64_t, NodeID> trackToNodeMap_;
    // Reverse map for quick lookup when polling telemetry
    std::unordered_map<uint64_t, TrackID> nodeToTrackMap_;

    // Cached meter levels per track
    std::unordered_map<uint64_t, TrackMeterState> trackMeterCache_;

    // Master bus meter state
    TrackMeterState masterMeterState_;

    // The NodeID of the master analysis node (its frames go to masterMeterState_)
    NodeID masterAnalysisNodeId_ = NodeID::invalid();

    // Constants for ballistics
    static constexpr double PEAK_ATTACK_MS = 10.0;    // Fast rise
    static constexpr double PEAK_DECAY_MS = 300.0;    // Slow fall (standard peak meter)
    static constexpr double RMS_ATTACK_MS = 300.0;    // Slow integration
    static constexpr double RMS_DECAY_MS = 300.0;     // Slow release
};

} // namespace bridge
