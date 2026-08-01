#pragma once
#include "itempo_service.h"
#include <vector>
#include <mutex>
#include <algorithm>

namespace Layer2 {

/// TempoMap - Storage for tempo events with sorted access
/// Thread-safety: Internally synchronized for non-RT modifications
/// RT-safe reads through const methods (using read-copy-update pattern)
class TempoMap {
public:
    TempoMap();
    explicit TempoMap(double defaultTempo);

    //==========================================================================
    // Non-RT-Safe Modification Methods
    //==========================================================================

    /// Set tempo at a specific position (replaces existing event at position)
    void setTempoAtPosition(double bpm, uint64_t position);

    /// Add a tempo event
    void addTempoEvent(const ITempoService::TempoPoint& event);

    /// Remove a tempo event at a specific position
    void removeTempoEventAtPosition(uint64_t position);

    /// Clear all tempo events
    void clear();

    //==========================================================================
    // RT-Safe Query Methods
    //==========================================================================

    /// Get tempo at a specific sample position
    /// Returns the tempo in BPM
    double getTempoAtPosition(uint64_t position) const;

    /// Find the tempo event at or before a position
    /// Returns true if found, false if only default tempo available
    bool findTempoEvent(uint64_t position, ITempoService::TempoPoint& outEvent) const;

    /// Get all tempo events within a range
    /// Returns number of events copied
    uint32_t getTempoRange(uint64_t start, uint64_t end,
                          ITempoService::TempoPoint* events,
                          uint32_t maxEvents) const;

    /// Get total number of tempo events
    size_t getEventCount() const { return tempoEvents.size(); }

    /// Access events directly (for snapshot/serialization)
    const std::vector<ITempoService::TempoPoint>& getEvents() const { return tempoEvents; }

    /// Replace all events (for snapshot/serialization)
    void setEvents(const ITempoService::TempoPoint* events, size_t count);

private:
    std::vector<ITempoService::TempoPoint> tempoEvents;
    double defaultBpm;
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

inline TempoMap::TempoMap()
    : TempoMap(120.0)
{}

inline TempoMap::TempoMap(double defaultTempo)
    : defaultBpm(defaultTempo)
{
    // Initialize with one default event at position 0
    tempoEvents.push_back(ITempoService::TempoPoint{0, defaultTempo, 4});
}

inline double TempoMap::getTempoAtPosition(uint64_t position) const
{
    // Read without lock (RT-safe) - tempoEvents is only modified when
    // audio is stopped, and we accept stale reads during modifications
    if (tempoEvents.empty()) {
        return defaultBpm;
    }

    size_t idx = lower_bound(position);

    if (idx == 0 && position < tempoEvents[0].positionSample) {
        // Position is before first event
        return tempoEvents[0].bpm;
    }

    if (idx == tempoEvents.size()) {
        // Position is after last event
        return tempoEvents.back().bpm;
    }

    // Check if we need the previous event
    if (tempoEvents[idx].positionSample > position && idx > 0) {
        return tempoEvents[idx - 1].bpm;
    }

    return tempoEvents[idx].bpm;
}

inline size_t TempoMap::lower_bound(uint64_t position) const
{
    // Binary search for the first event at or after position
    size_t left = 0;
    size_t right = tempoEvents.size();

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        if (tempoEvents[mid].positionSample < position) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    return left;
}

} // namespace Layer2
