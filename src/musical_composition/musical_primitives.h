#pragma once
#include "common/system_primitives.h"
#include <atomic>

namespace composition {

// Use the existing TypedHandle template from Layer 1/2
using TrackID  = ::TrackID;
using RegionID = ::TypedHandle<struct RegionTag>;
using NoteID   = ::TypedHandle<struct NoteTag>;
using ChordID  = ::TypedHandle<struct ChordTag>;
using LaneID   = ::TypedHandle<struct LaneTag>;
using TakeID   = ::TypedHandle<struct TakeTag>;
using SourceID = ::TypedHandle<struct SourceTag>;

inline std::atomic<uint32_t>& getGlobalClipIdCounter() {
    static std::atomic<uint32_t> counter{1000};
    return counter;
}

/**
 * @brief Helper to convert handles to uint64_t for storage or delta payloads.
 */
template<typename HandleType>
inline uint64_t handleToUint64(const HandleType& handle) {
    return (static_cast<uint64_t>(handle.generation) << 32) | handle.id;
}

template<typename HandleType>
inline HandleType uint64ToHandle(uint64_t raw) {
    return { static_cast<uint32_t>(raw & 0xFFFFFFFF), static_cast<uint32_t>(raw >> 32) };
}

struct MusicalPosition {
    uint32_t bar;
    uint16_t beat;
    uint32_t tick; // Ticks relative to start of beat
    uint32_t totalTicks; // Absolute ticks from project start
};

using ClipID = ::ClipID;
using RegionType = ::RegionType;

// Redefine TimelineRegion/MIDINote to use generation-counted IDs as per Layer 5 plan
struct TimelineRegion {
    RegionType type;
    SourceID sourceId;
    uint64_t positionSample;
    uint64_t sourceStartSample;
    uint64_t sourceLength;
    uint32_t fadeInSamples;
    uint32_t fadeOutSamples;
    float gain;
    bool isMuted;

    // Musical-time anchor — the primary truth for this region's start.
    // positionSample is a derived cache; it must be recalculated via
    // ITempoService::bbtToSamples(startPosition) whenever the TempoMap changes.
    MusicalPosition startPosition{};

    // Warping Metadata (Phase 4)
    WarpMode warpMode = WarpMode::BYPASS;
    float playbackRatio = 1.0f;
    float sourceBpm = 120.0f;
};

struct MIDINote {
    NoteID noteId;
    MusicalPosition startPosition;
    MusicalPosition endPosition;
    uint64_t offsetSample;
    uint64_t durationSample;
    uint64_t startSample = 0; // Derived cache — recalculate from startPosition on TempoMap change
    uint64_t endSample = 0;   // Derived cache — recalculate from endPosition on TempoMap change
    uint8_t pitch;
    uint8_t velocity;
    uint8_t channel;
};

struct AutomationPoint {
    uint64_t positionSample;
    float value;
    uint8_t curveShape;
    float tension;
};

} // namespace composition
