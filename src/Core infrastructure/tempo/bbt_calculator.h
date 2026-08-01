#pragma once
#include "itempo_service.h"
#include <cstdint>
#include <cmath>

namespace Layer2 {

/// BBTCalculator - Detailed computation for Bar:Beat:Tick time
/// This class handles the complex logic of converting samples to BBT
/// when there are tempo and meter changes throughout the timeline.
class BBTCalculator {
public:
    BBTCalculator() = default;

    //==========================================================================
    // Primary Calculation Methods
    //==========================================================================

    /// Convert sample position to BBT context
    /// This processes the timeline from 0 to the target position,
    /// tracking tempo and meter changes
    static void calculateBBT(uint64_t targetSample,
                            uint64_t& outSamplesProcessed,
                            double& outBeatsAccumulated,
                            uint32_t& outBar,
                            uint32_t& outBeat);

    //==========================================================================
    // Helper Methods
    //==========================================================================

    /// Calculate how many samples are in one bar
    /// barSamples = (beatsPerBar * 60 * sampleRate) / tempo
    static constexpr uint64_t samplesPerBar(double beatsPerBar,
                                           double tempo,
                                           double sampleRate)
    {
        return static_cast<uint64_t>((beatsPerBar * 60.0 * sampleRate) / tempo);
    }

    /// Calculate how many samples are in one beat
    /// beatSamples = (60 * sampleRate) / tempo
    static constexpr uint64_t samplesPerBeat(double tempo, double sampleRate)
    {
        return static_cast<uint64_t>((60.0 * sampleRate) / tempo);
    }

    /// Calculate how many samples are in one tick
    /// tickSamples = beatSamples / ticksPerBeat
    static constexpr uint64_t samplesPerTick(double tempo,
                                            double sampleRate,
                                            uint32_t ticksPerBeat)
    {
        return samplesPerBeat(tempo, sampleRate) / ticksPerBeat;
    }

    /// Convert fractional beat to tick position
    static uint32_t beatFractionToTicks(double fractionalBeat,
                                                  uint32_t ticksPerBeat)
    {
        double wholeBeats;
        double fraction = std::modf(fractionalBeat, &wholeBeats);
        return static_cast<uint32_t>(fraction * ticksPerBeat);
    }
};

//==========================================================================
// Inline Implementations
//==========================================================================

inline void BBTCalculator::calculateBBT(uint64_t targetSample,
                                        uint64_t& outSamplesProcessed,
                                        double& outBeatsAccumulated,
                                        uint32_t& outBar,
                                        uint32_t& outBeat)
{
    // Simplified implementation for constant tempo
    // Full implementation would iterate through tempo events

    // Default: 120 BPM, 4/4 time, 48 kHz
    constexpr double DEFAULT_TEMPO = 120.0;
    constexpr double DEFAULT_SAMPLE_RATE = 48000.0;
    constexpr double BEATS_PER_BAR = 4.0;

    // Calculate total beats
    double totalBeats = (static_cast<double>(targetSample) * DEFAULT_TEMPO) /
                        (60.0 * DEFAULT_SAMPLE_RATE);

    outBeatsAccumulated = totalBeats;

    // Calculate bar and beat (1-based)
    double barFraction = totalBeats / BEATS_PER_BAR;
    double wholeBars;
    double beatsInBar = std::modf(barFraction, &wholeBars);

    outBar = static_cast<uint32_t>(wholeBars) + 1;
    outBeat = static_cast<uint32_t>(beatsInBar * BEATS_PER_BAR) + 1;

    outSamplesProcessed = targetSample;
}

} // namespace Layer2
