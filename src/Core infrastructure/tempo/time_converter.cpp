#include "time_converter.h"
#include "bbt_calculator.h"
#include <cmath>
#include <algorithm>

namespace Layer2 {

// Type alias for convenience
using TempoPoint = ITempoService::TempoPoint;

//==============================================================================
// TimeConverter Implementation
//==============================================================================

uint64_t TimeConverter::beatsToSamples(double beats, double sampleRate) const
{
    const auto& events = tempoMap->getEvents();
    if (events.empty()) {
        return 0;
    }

    uint64_t accumulatedSamples = 0;
    double accumulatedBeats = 0.0;

    for (size_t i = 0; i < events.size(); ++i) {
        double currentTempo = events[i].bpm;
        uint64_t currentSampleStart = events[i].positionSample;
        
        if (i + 1 < events.size()) {
            uint64_t nextSampleStart = events[i + 1].positionSample;
            uint64_t samplesInSegment = nextSampleStart - currentSampleStart;
            double beatsInSegment = (static_cast<double>(samplesInSegment) * currentTempo) / (60.0 * sampleRate);
            
            if (accumulatedBeats + beatsInSegment >= beats) {
                double remainingBeats = beats - accumulatedBeats;
                uint64_t remainingSamples = static_cast<uint64_t>((remainingBeats * 60.0 * sampleRate) / currentTempo);
                return accumulatedSamples + remainingSamples;
            } else {
                accumulatedBeats += beatsInSegment;
                accumulatedSamples += samplesInSegment;
            }
        } else {
            double remainingBeats = beats - accumulatedBeats;
            uint64_t remainingSamples = static_cast<uint64_t>((remainingBeats * 60.0 * sampleRate) / currentTempo);
            return accumulatedSamples + remainingSamples;
        }
    }

    return accumulatedSamples;
}

double TimeConverter::samplesToBeats(uint64_t samples, double sampleRate) const
{
    const auto& events = tempoMap->getEvents();
    if (events.empty()) {
        return 0.0;
    }

    double accumulatedBeats = 0.0;

    for (size_t i = 0; i < events.size(); ++i) {
        double currentTempo = events[i].bpm;
        uint64_t currentSampleStart = events[i].positionSample;
        
        if (i + 1 < events.size()) {
            uint64_t nextSampleStart = events[i + 1].positionSample;
            
            if (samples < nextSampleStart) {
                uint64_t remainingSamples = samples - currentSampleStart;
                double remainingBeats = (static_cast<double>(remainingSamples) * currentTempo) / (60.0 * sampleRate);
                return accumulatedBeats + remainingBeats;
            } else {
                uint64_t samplesInSegment = nextSampleStart - currentSampleStart;
                double beatsInSegment = (static_cast<double>(samplesInSegment) * currentTempo) / (60.0 * sampleRate);
                accumulatedBeats += beatsInSegment;
            }
        } else {
            uint64_t remainingSamples = samples > currentSampleStart ? samples - currentSampleStart : 0;
            double remainingBeats = (static_cast<double>(remainingSamples) * currentTempo) / (60.0 * sampleRate);
            return accumulatedBeats + remainingBeats;
        }
    }

    return accumulatedBeats;
}

BBTPosition TimeConverter::samplesToBBT(uint64_t samples,
                                              double sampleRate,
                                              uint32_t ticksPerBeat) const
{
    BBTPosition result = {1, 1, 0, 0};
    if (samples == 0) return result;

    uint8_t meterNum, meterDen;
    double beatsPerBar = 4.0;
    if (meterMap->getMeterAtPosition(samples, meterNum, meterDen)) {
        beatsPerBar = static_cast<double>(meterNum);
    }

    double totalBeats = samplesToBeats(samples, sampleRate);

    double barFraction = totalBeats / beatsPerBar;
    double wholeBars;
    double beatsInBar = std::modf(barFraction, &wholeBars);

    result.bar = static_cast<uint32_t>(wholeBars) + 1;

    double wholeBeatsInBar;
    double beatFraction = std::modf(beatsInBar * beatsPerBar, &wholeBeatsInBar);
    result.beat = static_cast<uint32_t>(wholeBeatsInBar) + 1;
    result.tick = static_cast<uint32_t>(beatFraction * ticksPerBeat);

    return result;
}

uint64_t TimeConverter::bbtToSamples(const BBTPosition& bbt,
                                    double sampleRate,
                                    uint32_t ticksPerBeat) const
{
    uint8_t meterNum, meterDen;
    double beatsPerBar = 4.0;
    if (meterMap->getMeterAtPosition(0, meterNum, meterDen)) {
        beatsPerBar = static_cast<double>(meterNum);
    }

    double totalBeats = (static_cast<double>(bbt.bar - 1) * beatsPerBar) +
                       (static_cast<double>(bbt.beat - 1)) +
                       (static_cast<double>(bbt.tick) / static_cast<double>(ticksPerBeat));

    return beatsToSamples(totalBeats, sampleRate);
}

uint64_t TimeConverter::beatsToSamplesAt(double beats,
                                        uint64_t position,
                                        double sampleRate) const
{
    double tempo = tempoMap->getTempoAtPosition(position);
    return static_cast<uint64_t>((beats * 60.0 * sampleRate) / tempo);
}

double TimeConverter::samplesToBeatsFrom(uint64_t samples,
                                        uint64_t fromPosition,
                                        double sampleRate) const
{
    double tempo = tempoMap->getTempoAtPosition(fromPosition);
    return (static_cast<double>(samples) * tempo) / (60.0 * sampleRate);
}

uint64_t TimeConverter::calculateBBTSamples(uint32_t bar,
                                          uint32_t beat,
                                          uint32_t tick,
                                          double sampleRate,
                                          uint32_t ticksPerBeat) const
{
    BBTPosition bbt = {bar, beat, tick, 0};
    return bbtToSamples(bbt, sampleRate, ticksPerBeat);
}

} // namespace Layer2
