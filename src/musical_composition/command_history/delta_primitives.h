#pragma once
#include <cstdint>
#include <vector>

namespace composition {

enum class SubsystemID : uint8_t {
    TRACK_MANAGER = 0,
    PLAYLIST = 1,
    MIDI_SEQUENCER = 2,
    AUTOMATION = 3,
    ARRANGER = 4,
    CHORD_TRACK = 5,
    COMPING = 6,
    MIXER_ROUTING = 7,
    TEMPO_TIMELINE = 8,
    MARKER_TRACK = 9,
    SOURCE_MANAGER = 10,
    PROJECT_METADATA = 11,
    KEY_SIGNATURE_MAP = 12,
    REGION_METADATA = 13,
    COUNT = 14
};

/**
 * @brief An atomic state change that can be executed forward or backward.
 * Fixed-size 1120-byte payload buffer for newState and oldState.
 */
struct ProjectDelta {
    SubsystemID subsystemId;
    uint32_t operationType;
    uint64_t targetId;
    
    uint16_t newStateSize;
    uint8_t newState[1120];
    
    uint16_t oldStateSize;
    uint8_t oldState[1120];
};

static_assert(sizeof(ProjectDelta) <= 2300, "ProjectDelta should be compact");

/**
 * @brief A group of ProjectDeltas that undo/redo as a single atomic step.
 *
 * Produced by ICommandHistory::beginCompound() / endCompound(). On undo,
 * steps are replayed in reverse order; on redo, they are replayed forward.
 * All ownership and dispatch is NRT-only — nothing on the RT path touches this.
 */
struct CompoundDelta {
    std::vector<ProjectDelta> steps;
};

} // namespace composition
