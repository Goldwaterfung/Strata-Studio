#include "tempo_map.h"
#include <algorithm>

namespace Layer2 {

using TempoPoint = ITempoService::TempoPoint;

//==============================================================================
// TempoMap Implementation
//==============================================================================

void TempoMap::setTempoAtPosition(double bpm, uint64_t position)
{
    std::lock_guard<std::mutex> lock(mutex);

    // Check if there's already an event at this position
    auto it = std::find_if(tempoEvents.begin(), tempoEvents.end(),
                          [position](const TempoPoint& e) {
                              return e.positionSample == position;
                          });

    if (it != tempoEvents.end()) {
        // Update existing event
        it->bpm = bpm;
    } else {
        // Add new event
        tempoEvents.push_back(TempoPoint{position, bpm, 4});
        sortEvents();
    }
}

void TempoMap::addTempoEvent(const TempoPoint& event)
{
    std::lock_guard<std::mutex> lock(mutex);

    tempoEvents.push_back(event);
    sortEvents();
}

void TempoMap::removeTempoEventAtPosition(uint64_t position)
{
    if (position == 0) return; // Baseline tempo at 0 cannot be deleted

    std::lock_guard<std::mutex> lock(mutex);
    auto it = std::find_if(tempoEvents.begin(), tempoEvents.end(),
                          [position](const TempoPoint& e) {
                              return e.positionSample == position;
                          });

    if (it != tempoEvents.end()) {
        tempoEvents.erase(it);
    }
}

void TempoMap::clear()
{
    std::lock_guard<std::mutex> lock(mutex);

    tempoEvents.clear();
    // Add back default event
    tempoEvents.push_back(TempoPoint{0, defaultBpm, 4});
}

bool TempoMap::findTempoEvent(uint64_t position, TempoPoint& outEvent) const
{
    if (tempoEvents.empty()) {
        outEvent = TempoPoint{0, defaultBpm, 4};
        return false;
    }

    size_t idx = lower_bound(position);

    if (idx == 0 && position < tempoEvents[0].positionSample) {
        // Position is before first event - still return first event
        outEvent = tempoEvents[0];
        return true;
    }

    if (idx == tempoEvents.size()) {
        // Position is after last event
        outEvent = tempoEvents.back();
        return true;
    }

    // Check if we need the previous event
    if (tempoEvents[idx].positionSample > position && idx > 0) {
        outEvent = tempoEvents[idx - 1];
        return true;
    }

    outEvent = tempoEvents[idx];
    return true;
}

uint32_t TempoMap::getTempoRange(uint64_t start, uint64_t end,
                                TempoPoint* events,
                                uint32_t maxEvents) const
{
    if (!events || maxEvents == 0) {
        return 0;
    }

    uint32_t count = 0;

    for (const auto& event : tempoEvents) {
        if (event.positionSample >= start && event.positionSample <= end) {
            events[count++] = event;
            if (count >= maxEvents) {
                break;
            }
        }
    }

    return count;
}

void TempoMap::setEvents(const TempoPoint* events, size_t count)
{
    std::lock_guard<std::mutex> lock(mutex);

    tempoEvents.clear();
    tempoEvents.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        tempoEvents.push_back(events[i]);
    }

    sortEvents();
}

//==============================================================================
// Internal Helpers
//==============================================================================

void TempoMap::sortEvents()
{
    std::sort(tempoEvents.begin(), tempoEvents.end(),
             [](const TempoPoint& a, const TempoPoint& b) {
                 return a.positionSample < b.positionSample;
             });
}

size_t TempoMap::findEventIndex(uint64_t position) const
{
    return lower_bound(position);
}

} // namespace Layer2
