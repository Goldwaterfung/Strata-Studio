#include "itempo_service.h"
#include "tempo_map.h"
#include "meter_map.h"
#include "time_converter.h"
#include "bbt_calculator.h"
#include <memory>
#include <cstring>

namespace Layer2 {

// Type aliases for convenience
using TempoPoint = ITempoService::TempoPoint;
using MeterPoint = ITempoService::MeterPoint;

//==============================================================================
// TempoService Implementation
//==============================================================================

class TempoServiceImpl : public ITempoService {
private:
    std::unique_ptr<TempoMap> tempoMap;
    std::unique_ptr<MeterMap> meterMap;
    std::unique_ptr<TimeConverter> converter;

    double sampleRate;
    uint32_t ticksPerBeat;

    // Phase 1 Latched State
    ProcessContext latchedContext;

public:
    TempoServiceImpl()
        : sampleRate(48000.0)    // Default to 48 kHz
        , ticksPerBeat(960)       // Standard MIDI resolution
    {
        std::memset(&latchedContext, 0, sizeof(ProcessContext));
        tempoMap = std::make_unique<TempoMap>();
        meterMap = std::make_unique<MeterMap>();
        converter = std::make_unique<TimeConverter>(tempoMap.get(), meterMap.get());
    }

    //==========================================================================
    // ITempoService Implementation
    //==========================================================================

    void updateForCycle(const ProcessContext& context) override
    {
        latchedContext = context;
        // Automatically sync sample rate if it changed in context
        if (context.sampleRate > 0.0f && static_cast<double>(context.sampleRate) != sampleRate) {
            sampleRate = static_cast<double>(context.sampleRate);
        }
    }

    uint64_t getCyclePositionSamples() const override
    {
        return latchedContext.transport.positionSample;
    }

    double getCycleBPM() const override
    {
        return latchedContext.transport.bpm;
    }

    BBTPosition getCycleBBT() const override
    {
        BBTPosition bbt;
        bbt.bar = latchedContext.transport.bar;
        bbt.beat = latchedContext.transport.beat;
        bbt.tick = latchedContext.transport.tick;
        return bbt;
    }

    //==========================================================================
    // Tempo Management
    //==========================================================================

    void setTempoAtPosition(double bpm, uint64_t position) override
    {
        tempoMap->setTempoAtPosition(bpm, position);
    }

    void addTempoEvent(const TempoPoint& event) override
    {
        tempoMap->addTempoEvent(event);
    }

    void removeTempoEventAtPosition(uint64_t position) override
    {
        tempoMap->removeTempoEventAtPosition(position);
    }

    void clearTempoMap() override
    {
        tempoMap->clear();
    }

    //==========================================================================
    // Meter Management
    //==========================================================================

    void setMeterAtPosition(uint8_t numerator, uint8_t denominator,
                           uint64_t position) override
    {
        meterMap->setMeterAtPosition(numerator, denominator, position);
    }

    void addMeterEvent(const MeterPoint& event) override
    {
        meterMap->addMeterEvent(event);
    }

    void clearMeterMap() override
    {
        meterMap->clear();
    }

    //==========================================================================
    // RT-Safe Queries
    //==========================================================================

    double getTempoAtPosition(uint64_t position) const override
    {
        return tempoMap->getTempoAtPosition(position);
    }

    bool getMeterAtPosition(uint64_t position,
                           uint8_t& outNumerator,
                           uint8_t& outDenominator) const override
    {
        return meterMap->getMeterAtPosition(position, outNumerator, outDenominator);
    }

    //==========================================================================
    // Time Conversion (RT-Safe)
    //==========================================================================

    uint64_t beatsToSamples(double beats) const override
    {
        return converter->beatsToSamples(beats, sampleRate);
    }

    double samplesToBeats(uint64_t samples) const override
    {
        return converter->samplesToBeats(samples, sampleRate);
    }

    BBTPosition samplesToBBT(uint64_t samples) const override
    {
        return converter->samplesToBBT(samples, sampleRate, ticksPerBeat);
    }

    uint64_t bbtToSamples(const BBTPosition& bbt) const override
    {
        return converter->bbtToSamples(bbt, sampleRate, ticksPerBeat);
    }

    //==========================================================================
    // Range Queries
    //==========================================================================

    uint32_t getTempoRange(uint64_t start, uint64_t end,
                          TempoPoint* events,
                          uint32_t maxEvents) const override
    {
        return tempoMap->getTempoRange(start, end, events, maxEvents);
    }

    uint32_t getMeterRange(uint64_t start, uint64_t end,
                          MeterPoint* events,
                          uint32_t maxEvents) const override
    {
        return meterMap->getMeterRange(start, end, events, maxEvents);
    }

    //==========================================================================
    // Configuration
    //==========================================================================

    void setSampleRate(double newSampleRate) override
    {
        sampleRate = newSampleRate;
    }

    double getSampleRate() const override
    {
        return sampleRate;
    }

    void setTicksPerBeat(uint32_t ticks) override
    {
        ticksPerBeat = ticks;
    }

    uint32_t getTicksPerBeat() const override
    {
        return ticksPerBeat;
    }
};

//==============================================================================
// Factory Implementation
//==============================================================================

std::unique_ptr<ITempoService> ITempoService::create()
{
    return std::make_unique<TempoServiceImpl>();
}

} // namespace Layer2
