#pragma once
#include "system_primitives.h"
#include <memory>
#include <cstdint>

namespace Layer2 {

// BBT (Bar:Beat:Tick) structure - extracted from TimePosition for clarity
struct BBTPosition {
    uint32_t bar;        // Bar number (1-based)
    uint32_t beat;       // Beat within bar (1-based)
    uint32_t tick;       // Tick within beat (0-based)
    uint32_t reserved;   // Reserved for future use

    constexpr BBTPosition() : bar(1), beat(1), tick(0), reserved(0) {}
    constexpr BBTPosition(uint32_t b, uint32_t be, uint32_t t)
        : bar(b), beat(be), tick(t), reserved(0) {}
    constexpr BBTPosition(uint32_t b, uint32_t be, uint32_t t, uint32_t r)
        : bar(b), beat(be), tick(t), reserved(r) {}
};

// Type alias for compatibility with the interface specification
using BBT = BBTPosition;

class ITempoService {
public:
    //==========================================================================
    // Tempo Event (TRUE POD)
    //==========================================================================

    struct TempoPoint {
        uint64_t positionSample;    // Sample position of this tempo change
        double bpm;                 // Tempo in BPM (1.0 to 1000.0)
        uint8_t noteType;           // Note value for beat (4 = quarter note)
        uint8_t reserved[7];        // Padding for 16-byte alignment

        constexpr TempoPoint() : positionSample(0), bpm(120.0), noteType(4), reserved{} {}
        constexpr TempoPoint(uint64_t pos, double b, uint8_t nt = 4)
            : positionSample(pos), bpm(b), noteType(nt), reserved{} {}
    };

    //==========================================================================
    // Meter/Time Signature Event (TRUE POD)
    //==========================================================================

    struct MeterPoint {
        uint64_t positionSample;    // Sample position of this meter change
        uint8_t numerator;          // Upper number (3, 4, 7, etc.)
        uint8_t denominator;        // Lower number (2, 4, 8, 16 - power of 2)
        uint8_t reserved[6];        // Padding for 16-byte alignment

        constexpr MeterPoint() : positionSample(0), numerator(4), denominator(4), reserved{} {}
        constexpr MeterPoint(uint64_t pos, uint8_t num, uint8_t den)
            : positionSample(pos), numerator(num), denominator(den), reserved{} {}

        constexpr bool isValid() const {
            return numerator > 0 &&
                   (denominator == 2 || denominator == 4 ||
                    denominator == 8 || denominator == 16);
        }
    };

    // Compile-time POD verification (C++20: use is_trivially_copyable + is_standard_layout)
    static_assert(std::is_trivially_copyable<TempoPoint>::value &&
                  std::is_standard_layout<TempoPoint>::value,
                  "TempoPoint must be Plain Old Data (trivially copyable + standard layout)");
    static_assert(std::is_trivially_copyable<MeterPoint>::value &&
                  std::is_standard_layout<MeterPoint>::value,
                  "MeterPoint must be Plain Old Data (trivially copyable + standard layout)");

    //==========================================================================
    // Tempo Management (Non-Real-Time-Safe)
    // These methods may allocate memory and should only be called from the
    // main thread when the audio engine is stopped or between process() calls.
    //==========================================================================

    /// Set tempo at a specific sample position
    /// Thread-safety: NOT RT-safe, may allocate
    virtual void setTempoAtPosition(double bpm, uint64_t position) = 0;

    /// Add a tempo event to the tempo map
    /// Thread-safety: NOT RT-safe, may allocate
    virtual void addTempoEvent(const TempoPoint& event) = 0;

    /// Remove a tempo event at a specific sample position
    /// Thread-safety: NOT RT-safe
    virtual void removeTempoEventAtPosition(uint64_t position) = 0;

    /// Remove all tempo events and reset to default
    /// Thread-safety: NOT RT-safe
    virtual void clearTempoMap() = 0;

    //==========================================================================
    // Meter Management (Non-Real-Time-Safe)
    //==========================================================================

    /// Set time signature at a specific sample position
    /// Thread-safety: NOT RT-safe, may allocate
    virtual void setMeterAtPosition(uint8_t numerator, uint8_t denominator,
                                   uint64_t position) = 0;

    /// Add a meter event to the meter map
    /// Thread-safety: NOT RT-safe, may allocate
    virtual void addMeterEvent(const MeterPoint& event) = 0;

    /// Remove all meter events and reset to default
    /// Thread-safety: NOT RT-safe
    virtual void clearMeterMap() = 0;

    //==========================================================================
    // Real-Time Safe Queries
    // These methods are wait-free and safe to call from the audio thread.
    //==========================================================================

    /// Get the tempo (BPM) at a given sample position
    /// Thread-safety: RT-safe, wait-free
    virtual double getTempoAtPosition(uint64_t position) const = 0;

    /// Get the meter/time signature at a given sample position
    /// Thread-safety: RT-safe, wait-free
    /// Returns: true if meter found, false otherwise (outputs unchanged)
    virtual bool getMeterAtPosition(uint64_t position,
                                   uint8_t& outNumerator,
                                   uint8_t& outDenominator) const = 0;

    //==========================================================================
    // Cycle-Aware Operations (Phase 1 Sync)
    //==========================================================================

    /**
     * @brief Latch the transport state for the upcoming cycle (Phase 1)
     * @param context The process context containing latched transport and clock data
     * @thread_safety RT-safe, Wait-free
     */
    virtual void updateForCycle(const ProcessContext& context) = 0;

    /**
     * @brief Get the sample position at the start of the current cycle
     */
    virtual uint64_t getCyclePositionSamples() const = 0;

    /**
     * @brief Get the BPM latched for the current cycle
     */
    virtual double getCycleBPM() const = 0;

    /**
     * @brief Get the Bar:Beat:Tick position at the start of the current cycle
     */
    virtual BBTPosition getCycleBBT() const = 0;

    //==========================================================================
    // Time Conversion (Real-Time Safe)
    //==========================================================================

    /// Convert beats to samples
    /// Thread-safety: RT-safe, wait-free
    virtual uint64_t beatsToSamples(double beats) const = 0;

    /// Convert samples to beats
    /// Thread-safety: RT-safe, wait-free
    virtual double samplesToBeats(uint64_t samples) const = 0;

    /// Convert samples to Bar:Beat:Tick time
    /// Thread-safety: RT-safe, wait-free
    /// Uses 960 ticks per beat as standard (MIDI resolution)
    virtual BBTPosition samplesToBBT(uint64_t samples) const = 0;

    /// Convert Bar:Beat:Tick to samples
    /// Thread-safety: RT-safe, wait-free
    virtual uint64_t bbtToSamples(const BBTPosition& bbt) const = 0;

    //==========================================================================
    // Range Queries (For UI Display)
    // These methods are optimized for retrieving multiple events for display.
    //==========================================================================

    /// Get tempo events within a sample range
    /// Thread-safety: RT-safe (no allocation)
    /// Returns: number of events copied
    virtual uint32_t getTempoRange(uint64_t start, uint64_t end,
                                  TempoPoint* events,
                                  uint32_t maxEvents) const = 0;

    /// Get meter events within a sample range
    /// Thread-safety: RT-safe (no allocation)
    /// Returns: number of events copied
    virtual uint32_t getMeterRange(uint64_t start, uint64_t end,
                                  MeterPoint* events,
                                  uint32_t maxEvents) const = 0;

    //==========================================================================
    // Configuration
    //==========================================================================

    /// Set the sample rate for time conversions
    /// Thread-safety: NOT RT-safe, call during initialization only
    virtual void setSampleRate(double sampleRate) = 0;

    /// Get the current sample rate
    /// Thread-safety: RT-safe
    virtual double getSampleRate() const = 0;

    /// Set the ticks per beat for BBT calculations
    /// Default is 960 (standard MIDI resolution)
    /// Thread-safety: NOT RT-safe, call during initialization only
    virtual void setTicksPerBeat(uint32_t ticksPerBeat) = 0;

    /// Get the ticks per beat
    /// Thread-safety: RT-safe
    virtual uint32_t getTicksPerBeat() const = 0;

    //==========================================================================
    // Factory
    //==========================================================================

    static std::unique_ptr<ITempoService> create();

    virtual ~ITempoService() = default;
};

} // namespace Layer2
