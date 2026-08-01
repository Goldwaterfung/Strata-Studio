#pragma once
#include "tracks/itrack_controller.h"

namespace bridge {

/// Canonical main-lane height for a track
constexpr double kMainLaneHeightInstrument = 120.0;
constexpr double kMainLaneHeightDefault    = 120.0;

/**
 * @brief Get the height of the main lane of a track.
 */
inline double mainLaneHeightForTrack(const TrackUIState& t) noexcept {
    return t.hasInstrumentSlot ? kMainLaneHeightInstrument : kMainLaneHeightDefault;
}

/**
 * @brief Get the sum of the heights of all expanded sub-lanes for a track.
 */
inline double accumulatedSubLaneHeight(const TrackUIState& t) noexcept {
    double h = 0.0;
    if (t.isAutomationExpanded) {
        for (uint32_t i = 0; i < t.activeSubLaneCount; ++i) {
            if (t.subLanes[i].isExpanded) {
                h += static_cast<double>(t.subLanes[i].heightPx);
            }
        }
    }
    return h;
}

/**
 * @brief Get the total height of a track including all expanded sub-lanes.
 */
inline double totalTrackHeight(const TrackUIState& t) noexcept {
    return mainLaneHeightForTrack(t) + accumulatedSubLaneHeight(t);
}

/**
 * @brief Get Y offset of a specific sub-lane within a track (relative to track top).
 *        Returns -1.0 if the target parameter's sub-lane is not found or collapsed.
 */
inline double subLaneOffsetInTrack(const TrackUIState& t, NodeID targetNode, uint32_t paramIndex) noexcept {
    double y = mainLaneHeightForTrack(t);
    if (!t.isAutomationExpanded) {
        return -1.0;
    }
    for (uint32_t i = 0; i < t.activeSubLaneCount; ++i) {
        const auto& sl = t.subLanes[i];
        if (!sl.isExpanded) {
            continue;
        }
        if (sl.targetNodeId == targetNode && sl.parameterIndex == paramIndex) {
            return y;
        }
        y += static_cast<double>(sl.heightPx);
    }
    return -1.0;
}

/**
 * @brief Get the height of a specific sub-lane within a track.
 *        Returns 50.0 (default fallback) if not found or collapsed.
 */
inline double subLaneHeightInTrack(const TrackUIState& t, NodeID targetNode, uint32_t paramIndex) noexcept {
    if (!t.isAutomationExpanded) {
        return 50.0;
    }
    for (uint32_t i = 0; i < t.activeSubLaneCount; ++i) {
        const auto& sl = t.subLanes[i];
        if (sl.targetNodeId == targetNode && sl.parameterIndex == paramIndex) {
            return static_cast<double>(sl.heightPx);
        }
    }
    return 50.0;
}

/**
 * @brief Struct to represent hit testing results on a track's sub-lanes.
 */
struct SubLaneHit {
    int32_t index;
    NodeID nodeId;
    uint32_t paramIndex;
};

/**
 * @brief Find which sub-lane a relative Y offset falls on.
 *        Returns {index, nodeId, paramIndex} or {-1, invalid, 0} if it falls on the main lane or is out of bounds.
 */
inline SubLaneHit subLaneAtY(const TrackUIState& t, double relY) noexcept {
    double y = mainLaneHeightForTrack(t);
    if (!t.isAutomationExpanded || relY < y) {
        return {-1, NodeID::invalid(), 0};
    }
    for (uint32_t i = 0; i < t.activeSubLaneCount; ++i) {
        const auto& sl = t.subLanes[i];
        if (!sl.isExpanded) {
            continue;
        }
        double laneH = static_cast<double>(sl.heightPx);
        if (relY >= y && relY < y + laneH) {
            return {static_cast<int32_t>(i), sl.targetNodeId, sl.parameterIndex};
        }
        y += laneH;
    }
    return {-1, NodeID::invalid(), 0};
}

} // namespace bridge
