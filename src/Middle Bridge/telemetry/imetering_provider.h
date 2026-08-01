#pragma once
#include "common/system_primitives.h"
#include <cstdint>
#include <vector>
#include <cmath>

namespace bridge {

/**
 * @brief Thread-safe Plain Old Data representing highly-responsive, 
 * smoothed metering levels for stereo/mono UI channels.
 */
struct MeterLevel {
    float peakLeft = -120.0f;   // Smoothed peak level in dB (-120.0 to +6.0)
    float peakRight = -120.0f;  // Smoothed peak level in dB (-120.0 to +6.0)
    float rmsLeft = -120.0f;    // Smoothed RMS level in dB
    float rmsRight = -120.0f;   // Smoothed RMS level in dB
    bool clipLeft = false;      // Clip indicator (sticky until reset)
    bool clipRight = false;     // Clip indicator (sticky until reset)
};

/**
 * @brief Interface for pulling smoothed, ballistics-processed metering 
 * and spectral analysis data optimized for 60Hz/120Hz UI rendering.
 */
class IMeteringProvider {
public:
    virtual ~IMeteringProvider() = default;

    // --- Fader & Channel Metering ---
    
    /**
     * @brief Pulls the current smoothed meter levels for a track.
     * @param id The ID of the target track (from Layer 5).
     * @return The stereo MeterLevel structure containing smoothed dB values.
     */
    virtual MeterLevel getTrackLevels(TrackID id) = 0;

    /**
     * @brief Resets the sticky clipping indicators for a specific track.
     */
    virtual void resetTrackClip(TrackID id) = 0;

    // --- Master & Main Bus Metering ---
    
    /**
     * @brief Pulls the current master output meter levels.
     */
    virtual MeterLevel getMasterLevels() = 0;
    
    /**
     * @brief Resets the master clip indicators.
     */
    virtual void resetMasterClip() = 0;

    // --- Mapping Registry ---

    /**
     * @brief Registers a mapping between a TrackID and the corresponding Analysis NodeID.
     */
    virtual void registerTrackNodeMapping(TrackID trackId, NodeID nodeId) = 0;

    /**
     * @brief Unregisters a track from its analysis node mapping.
     */
    virtual void unregisterTrackNodeMapping(TrackID trackId) = 0;

    // --- Periodic Updates ---

    /**
     * @brief Polls telemetry and processes HSL ballistics.
     * Called from the main GUI loop or GUI timer thread at 60Hz/120Hz.
     * @param elapsedMilliseconds Time passed since last update in milliseconds.
     */
    virtual void updateMeters(double elapsedMilliseconds) = 0;

    // --- Spectral / FFT Visualizations ---

    /**
     * @brief Retrieves real-time spectral analyzer data.
     * @param analyzerNodeId NodeID of the DSP analyzer node (from Layer 4).
     * @param outMagnitudes Pre-allocated array to receive the FFT magnitude bins.
     * @param binCount The number of FFT bins requested.
     */
    virtual void getSpectrumData(NodeID analyzerNodeId, float* outMagnitudes, uint32_t binCount) = 0;
};

/**
 * @brief A standard exponential decay filter providing accurate HSL ballistics.
 * Designed to run on the GUI pull/timer thread, avoiding CPU overhead on the RT thread.
 */
class BallisticsFilter {
public:
    void init(double timerIntervalMs, double attackTimeMs, double releaseTimeMs) {
        if (timerIntervalMs <= 0.0) return;
        double timerFreq = 1000.0 / timerIntervalMs;
        // alpha = 1 - exp(-1.0 / (timerFreq * (timeMs / 1000.0)))
        attackCoeff_ = (attackTimeMs <= 0.0) ? 1.0f : 
            static_cast<float>(1.0 - std::exp(-1000.0 / (timerFreq * attackTimeMs)));
        releaseCoeff_ = (releaseTimeMs <= 0.0) ? 1.0f : 
            static_cast<float>(1.0 - std::exp(-1000.0 / (timerFreq * releaseTimeMs)));
    }

    float filter(float targetVal, float currentVal) {
        float coeff = (targetVal > currentVal) ? attackCoeff_ : releaseCoeff_;
        return currentVal + coeff * (targetVal - currentVal);
    }

private:
    float attackCoeff_ = 1.0f;
    float releaseCoeff_ = 1.0f;
};

} // namespace bridge
