#include "meter_map.h"
#include <algorithm>

namespace Layer2 {

using MeterPoint = ITempoService::MeterPoint;

//==============================================================================
// MeterMap Implementation
//==============================================================================

void MeterMap::setMeterAtPosition(uint8_t numerator, uint8_t denominator, uint64_t position)
{
    std::lock_guard<std::mutex> lock(mutex);

    // Check if there's already an event at this position
    auto it = std::find_if(meterEvents.begin(), meterEvents.end(),
                          [position](const MeterPoint& e) {
                              return e.positionSample == position;
                          });

    if (it != meterEvents.end()) {
        // Update existing event
        it->numerator = numerator;
        it->denominator = denominator;
    } else {
        // Add new event
        meterEvents.push_back(MeterPoint{position, numerator, denominator});
        sortEvents();
    }
}

void MeterMap::addMeterEvent(const MeterPoint& event)
{
    std::lock_guard<std::mutex> lock(mutex);

    meterEvents.push_back(event);
    sortEvents();
}

void MeterMap::clear()
{
    std::lock_guard<std::mutex> lock(mutex);

    meterEvents.clear();
    // Add back default event
    meterEvents.push_back(MeterPoint{0, defaultNumerator, defaultDenominator});
}

uint32_t MeterMap::getMeterRange(uint64_t start, uint64_t end,
                                MeterPoint* events,
                                uint32_t maxEvents) const
{
    if (!events || maxEvents == 0) {
        return 0;
    }

    uint32_t count = 0;

    for (const auto& event : meterEvents) {
        if (event.positionSample >= start && event.positionSample <= end) {
            events[count++] = event;
            if (count >= maxEvents) {
                break;
            }
        }
    }

    return count;
}

void MeterMap::setEvents(const MeterPoint* events, size_t count)
{
    std::lock_guard<std::mutex> lock(mutex);

    meterEvents.clear();
    meterEvents.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        meterEvents.push_back(events[i]);
    }

    sortEvents();
}

//==============================================================================
// Internal Helpers
//==============================================================================

void MeterMap::sortEvents()
{
    std::sort(meterEvents.begin(), meterEvents.end(),
             [](const MeterPoint& a, const MeterPoint& b) {
                 return a.positionSample < b.positionSample;
             });
}

size_t MeterMap::findEventIndex(uint64_t position) const
{
    return lower_bound(position);
}

} // namespace Layer2
