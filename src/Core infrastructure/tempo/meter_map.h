#pragma once
#include "itempo_service.h"
#include <vector>
#include <mutex>
#include <algorithm>

namespace Layer2 {

/// MeterMap - Storage for meter/time signature events with sorted access
/// Thread-safety: Internally synchronized for non-RT modifications
/// RT-safe reads through const methods (using read-copy-update pattern)
class MeterMap {
public:
    MeterMap();
    explicit MeterMap(uint8_t defaultNumerator, uint8_t defaultDenominator);

    //==========================================================================
    // Non-RT-Safe Modification Methods
    //==========================================================================

    /// Set meter at a specific position (replaces existing event at position)
    void setMeterAtPosition(uint8_t numerator, uint8_t denominator, uint64_t position);

    /// Add a meter event
    void addMeterEvent(const ITempoService::MeterPoint& event);

    /// Clear all meter events
    void clear();

    //==========================================================================
    // RT-Safe Query Methods
    //==========================================================================

    /// Get meter at a specific sample position
    /// Returns true if meter found, false otherwise
    bool getMeterAtPosition(uint64_t position,
                           uint8_t& outNumerator,
                           uint8_t& outDenominator) const;

    /// Get all meter events within a range
    /// Returns number of events copied
    uint32_t getMeterRange(uint64_t start, uint64_t end,
                          ITempoService::MeterPoint* events,
                          uint32_t maxEvents) const;

    /// Get total number of meter events
    size_t getEventCount() const { return meterEvents.size(); }

    /// Access events directly (for snapshot/serialization)
    const std::vector<ITempoService::MeterPoint>& getEvents() const { return meterEvents; }

    /// Replace all events (for snapshot/serialization)
    void setEvents(const ITempoService::MeterPoint* events, size_t count);

private:
    std::vector<ITempoService::MeterPoint> meterEvents;
    uint8_t defaultNumerator;
    uint8_t defaultDenominator;
    mutable std::mutex mutex;

    //==========================================================================
    // Internal Helpers
    //==========================================================================

    /// Sort events by position (maintains invariant)
    void sortEvents();

    /// Find the index of the event at or before position
    /// Returns: index of event, or SIZE_MAX if position is before first event
    size_t findEventIndex(uint64_t position) const;

    /// Binary search for the event index
    size_t lower_bound(uint64_t position) const;
};

//==========================================================================
// Inline Implementations
//==========================================================================

inline MeterMap::MeterMap()
    : MeterMap(4, 4)  // Default 4/4 time
{}

inline MeterMap::MeterMap(uint8_t defaultNumerator, uint8_t defaultDenominator)
    : defaultNumerator(defaultNumerator)
    , defaultDenominator(defaultDenominator)
{
    // Initialize with one default event at position 0
    meterEvents.push_back(ITempoService::MeterPoint{0, defaultNumerator, defaultDenominator});
}

inline bool MeterMap::getMeterAtPosition(uint64_t position,
                                        uint8_t& outNumerator,
                                        uint8_t& outDenominator) const
{
    // Read without lock (RT-safe) - meterEvents is only modified when
    // audio is stopped, and we accept stale reads during modifications
    if (meterEvents.empty()) {
        outNumerator = defaultNumerator;
        outDenominator = defaultDenominator;
        return false;
    }

    size_t idx = lower_bound(position);

    if (idx == 0 && position < meterEvents[0].positionSample) {
        // Position is before first event
        outNumerator = meterEvents[0].numerator;
        outDenominator = meterEvents[0].denominator;
        return true;
    }

    if (idx == meterEvents.size()) {
        // Position is after last event
        outNumerator = meterEvents.back().numerator;
        outDenominator = meterEvents.back().denominator;
        return true;
    }

    // Check if we need the previous event
    if (meterEvents[idx].positionSample > position && idx > 0) {
        outNumerator = meterEvents[idx - 1].numerator;
        outDenominator = meterEvents[idx - 1].denominator;
        return true;
    }

    outNumerator = meterEvents[idx].numerator;
    outDenominator = meterEvents[idx].denominator;
    return true;
}

inline size_t MeterMap::lower_bound(uint64_t position) const
{
    // Binary search for the first event at or after position
    size_t left = 0;
    size_t right = meterEvents.size();

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        if (meterEvents[mid].positionSample < position) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    return left;
}

} // namespace Layer2
