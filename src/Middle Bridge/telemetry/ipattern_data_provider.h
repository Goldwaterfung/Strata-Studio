#pragma once
#include "common/system_primitives.h"
#include "musical_composition/musical_primitives.h"

namespace bridge {

using composition::RegionID;

/**
 * @brief POD representation of a single MIDI note event for thumbnail rendering.
 *        Safe to copy across layer boundaries (no pointers, no virtual methods).
 */
struct VisualNoteEvent {
    uint64_t startFrame;
    uint32_t durationFrames;
    uint8_t  pitch;        // MIDI note 0-127
    uint8_t  velocity;     // 0-127
    uint8_t  _pad[2];      // Explicit padding for deterministic struct size
};

static_assert(std::is_trivially_copyable<VisualNoteEvent>::value,
              "VisualNoteEvent must be trivially copyable for cross-layer safety");

/**
 * @brief Provider interface for querying MIDI note data for pattern clip thumbnails.
 *
 * Layer 7 widgets use this to render compact piano-roll previews inside
 * pattern clips on the Playlist canvas. All methods are GUI-thread only.
 */
class IPatternDataProvider {
public:
    virtual ~IPatternDataProvider() = default;

    /**
     * @brief Retrieve note events for a pattern region, suitable for thumbnail rendering.
     *        Stack-allocated output buffer — no heap allocation.
     *
     * @param regionId  The pattern region to query
     * @param outEvents Caller-provided array to fill
     * @param maxCount  Maximum capacity of the caller array
     * @return Number of note events actually written
     */
    virtual uint32_t getNoteEventsForRegion(
        RegionID regionId,
        VisualNoteEvent* outEvents,
        uint32_t maxCount
    ) const = 0;
};

} // namespace bridge
