#pragma once
#include "itempo_service.h"
#include "tempo_map.h"
#include "meter_map.h"
#include <cstdint>

namespace Layer2 {

/// TimeConverter - Converts between different time domains
/// This class provides wait-free conversion between samples, beats, and BBT.
/// It maintains no internal state and delegates to TempoMap/MeterMap.
class TimeConverter {
public:
    TimeConverter(const TempoMap* tempo, const MeterMap* meter);

    //==========================================================================
    // Basic Conversions (RT-Safe, Wait-Free)
    //==========================================================================

    /// Convert beats to samples
    /// Note: For variable tempo, this calculates from position 0
    uint64_t beatsToSamples(double beats, double sampleRate) const;

    /// Convert samples to beats
    /// Note: For variable tempo, this calculates from position 0
    double samplesToBeats(uint64_t samples, double sampleRate) const;

    //==========================================================================
    // BBT Conversions (RT-Safe, Wait-Free)
    //==========================================================================

    /// Convert samples to Bar:Beat:Tick
    /// This is the most complex conversion, handling tempo and meter changes
    BBTPosition samplesToBBT(uint64_t samples,
                                   double sampleRate,
                                   uint32_t ticksPerBeat) const;

    /// Convert Bar:Beat:Tick to samples
    uint64_t bbtToSamples(const BBTPosition& bbt,
                         double sampleRate,
                         uint32_t ticksPerBeat) const;

    //==========================================================================
    // Tempo/Meter-Aware Conversions
    //==========================================================================

    /// Convert a duration in beats to samples at a specific position
    uint64_t beatsToSamplesAt(double beats,
                             uint64_t position,
                             double sampleRate) const;

    /// Convert samples to beats starting from a specific position
    double samplesToBeatsFrom(uint64_t samples,
                             uint64_t fromPosition,
                             double sampleRate) const;

private:
    const TempoMap* tempoMap;
    const MeterMap* meterMap;

    //==========================================================================
    // Internal Helpers
    //==========================================================================

    /// Calculate the sample position of a beat given bar/beat/tick
    uint64_t calculateBBTSamples(uint32_t bar,
                                uint32_t beat,
                                uint32_t tick,
                                double sampleRate,
                                uint32_t ticksPerBeat) const;

    /// Get beats per bar at a position
    double getBeatsPerBar(uint64_t position) const;

    /// Get samples per beat at a position
    double getSamplesPerBeat(double bpm, double sampleRate) const;
};

//==========================================================================
// Inline Implementations (for performance-critical paths)
//==========================================================================

inline TimeConverter::TimeConverter(const TempoMap* tempo, const MeterMap* meter)
    : tempoMap(tempo)
    , meterMap(meter)
{
    // Note: We don't own these pointers, they must outlive this instance
}

inline double TimeConverter::getSamplesPerBeat(double bpm, double sampleRate) const
{
    // samples_per_beat = (60 * sampleRate) / bpm
    return (60.0 * sampleRate) / bpm;
}

inline double TimeConverter::getBeatsPerBar(uint64_t position) const
{
    uint8_t numerator, denominator;
    if (meterMap->getMeterAtPosition(position, numerator, denominator)) {
        return static_cast<double>(numerator);
    }
    return 4.0;  // Default to 4 beats per bar
}

} // namespace Layer2
