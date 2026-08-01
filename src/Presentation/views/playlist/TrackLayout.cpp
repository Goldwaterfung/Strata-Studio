// src/Presentation/views/playlist/TrackLayout.cpp
#include "TrackLayout.h"
#include "theme.h"
#include "Middle Bridge/automation/automation_helpers.h"

namespace presentation::views {

double TrackLayout::getSubLaneHeightForParam(NodeID targetNode, uint32_t subNodeId, uint32_t paramIndex) const {
    for (const auto& sl : subLanes) {
        if (sl.targetNodeId == targetNode && sl.subNodeId == subNodeId && sl.parameterIndex == paramIndex) {
            return sl.height;
        }
    }
    return static_cast<double>(theme::Layout::DefaultSubLaneHeight);
}

double TrackLayout::getSubLaneOffsetForParam(NodeID targetNode, uint32_t subNodeId, uint32_t paramIndex) const {
    for (const auto& sl : subLanes) {
        if (sl.isExpanded && sl.targetNodeId == targetNode && sl.subNodeId == subNodeId && sl.parameterIndex == paramIndex) {
            return sl.relativeOffset;
        }
    }
    return -1.0;
}

TrackLayout::HitResult TrackLayout::hitSubLaneAtY(double relY) const {
    if (relY < mainLaneHeight) {
        return {-1, NodeID::invalid(), 0, 0};
    }
    for (const auto& sl : subLanes) {
        if (!sl.isExpanded) {
            continue;
        }
        if (relY >= sl.relativeOffset && relY < sl.relativeOffset + sl.height) {
            return {static_cast<int32_t>(sl.index), sl.targetNodeId, sl.subNodeId, sl.parameterIndex};
        }
    }
    return {-1, NodeID::invalid(), 0, 0};
}

std::vector<TrackLayout> buildTrackLayouts(
    const std::vector<bridge::TrackUIState>& tracks,
    const std::unordered_map<uint64_t, int>& trackHeights,
    const std::unordered_map<uint64_t, uint32_t>& autoSubLaneHeights,
    const std::map<std::pair<uint64_t, uint32_t>, int>& takesLaneHeights,
    double defaultTrackHeight)
{
    std::vector<TrackLayout> layouts;
    layouts.reserve(tracks.size());

    for (const auto& track : tracks) {
        TrackLayout layout;
        layout.trackId = track.trackId;
        layout.audioLanesCount = track.audioLanesCount;

        // 1. Determine main lane height
        auto it = trackHeights.find(track.trackId.toRaw());
        if (it != trackHeights.end()) {
            layout.mainLaneHeight = static_cast<double>(it->second);
        } else {
            layout.mainLaneHeight = track.hasInstrumentSlot ? bridge::kMainLaneHeightInstrument : defaultTrackHeight;
        }

        // 2. Process takes lanes
        double currentOffset = layout.mainLaneHeight;
        layout.isTakesExpanded = track.isTakesExpanded;
        if (track.isTakesExpanded && track.audioLanesCount > 1) {
            layout.takesLaneHeights.resize(track.audioLanesCount, static_cast<double>(theme::Layout::DefaultSubLaneHeight));
            for (uint32_t s = 1; s < track.audioLanesCount; ++s) {
                auto itTakes = takesLaneHeights.find({track.trackId.toRaw(), s});
                double h = (itTakes != takesLaneHeights.end()) ? itTakes->second : static_cast<double>(theme::Layout::DefaultSubLaneHeight);
                layout.takesLaneHeights[s] = h;
                currentOffset += h;
            }
        }

        // 3. Process automation sub-lanes
        if (track.isAutomationExpanded) {
            layout.subLanes.reserve(track.activeSubLaneCount);
            for (uint32_t i = 0; i < track.activeSubLaneCount; ++i) {
                const auto& slState = track.subLanes[i];
                SubLaneLayout sl;
                sl.index = i;
                sl.targetNodeId = slState.targetNodeId;
                sl.subNodeId = slState.subNodeId;
                sl.parameterIndex = slState.parameterIndex;
                sl.isExpanded = slState.isExpanded;

                if (sl.isExpanded) {
                    uint64_t key = (track.trackId.toRaw() << 16) | static_cast<uint64_t>(i);
                    auto subIt = autoSubLaneHeights.find(key);
                    if (subIt != autoSubLaneHeights.end()) {
                        sl.height = static_cast<double>(subIt->second);
                    } else {
                        sl.height = static_cast<double>(slState.heightPx);
                    }
                    sl.relativeOffset = currentOffset;
                    currentOffset += sl.height;
                } else {
                    sl.height = 0.0;
                    sl.relativeOffset = 0.0;
                }
                layout.subLanes.push_back(sl);
            }
        }

        layout.totalHeight = currentOffset;
        layouts.push_back(std::move(layout));
    }

    return layouts;
}

} // namespace presentation::views
