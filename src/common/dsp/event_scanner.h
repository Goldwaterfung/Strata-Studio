#pragma once

#include "../system_primitives.h"

namespace DSP {

/**
 * @brief Utility for efficiently scanning and processing sample-accurate events.
 * 
 * Used inside the DSPProcessFunc loop to trigger parameter changes or MIDI
 * events at the correct sample offset.
 */
class EventScanner {
public:
    EventScanner(const EventData* events, uint32_t numEvents)
        : _events(events)
        , _numEvents(numEvents)
        , _currentIndex(0) {}

    /**
     * @brief Processes all events that occur at or before the given sample offset.
     * 
     * @tparam Handler A functor/lambda with signature: void(const EventData&)
     * @param sampleOffset The current sample index in the processing loop.
     * @param handler The callback to execute for each event.
     */
    template <typename Handler>
    inline void processEventsAtOffset(uint32_t sampleOffset, Handler handler) {
        while (_currentIndex < _numEvents && _events[_currentIndex].sampleOffset <= sampleOffset) {
            handler(_events[_currentIndex]);
            _currentIndex++;
        }
    }

    /**
     * @brief Skips all events until the given offset without processing them.
     */
    inline void skipToOffset(uint32_t sampleOffset) {
        while (_currentIndex < _numEvents && _events[_currentIndex].sampleOffset < sampleOffset) {
            _currentIndex++;
        }
    }

    bool hasMoreEvents() const { return _currentIndex < _numEvents; }

    const EventData* peekNextEvent() const {
        return (_currentIndex < _numEvents) ? &_events[_currentIndex] : nullptr;
    }

private:
    const EventData* _events;
    uint32_t _numEvents;
    uint32_t _currentIndex;
};

} // namespace DSP
