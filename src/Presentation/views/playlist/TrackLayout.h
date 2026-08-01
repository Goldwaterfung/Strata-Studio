// src/Presentation/views/playlist/TrackLayout.h
#pragma once

#include "common/system_primitives.h"
#include "Middle Bridge/tracks/itrack_controller.h"
#include <vector>
#include <unordered_map>
#include <map>

namespace presentation::views {

struct SubLaneLayout {
    uint32_t index{0};
    NodeID targetNodeId;
    uint32_t subNodeId{0};
    uint32_t parameterIndex{0};
    double height{0.0};
    double relativeOffset{0.0}; // Offset from the top of the track
    bool isExpanded{false};
};

inline bool operator==(const SubLaneLayout& lhs, const SubLaneLayout& rhs) {
    return lhs.index == rhs.index &&
           lhs.targetNodeId == rhs.targetNodeId &&
           lhs.subNodeId == rhs.subNodeId &&
           lhs.parameterIndex == rhs.parameterIndex &&
           lhs.height == rhs.height &&
           lhs.relativeOffset == rhs.relativeOffset &&
           lhs.isExpanded == rhs.isExpanded;
}

struct TrackLayout {
    TrackID trackId;
    double mainLaneHeight{0.0};
    // Vector of heights for each take lane. Index 0 is unused (main lane).
    std::vector<double> takesLaneHeights;
    double totalHeight{0.0};
    uint32_t audioLanesCount{1};
    bool isTakesExpanded{false};
    std::vector<SubLaneLayout> subLanes;

    // Helper queries
    double getSubLaneHeightForParam(NodeID targetNode, uint32_t subNodeId, uint32_t paramIndex) const;
    double getSubLaneOffsetForParam(NodeID targetNode, uint32_t subNodeId, uint32_t paramIndex) const;
    
    struct HitResult {
        int32_t index{-1};
        NodeID nodeId;
        uint32_t subNodeId{0};
        uint32_t paramIndex{0};
    };
    HitResult hitSubLaneAtY(double relY) const;
};

inline bool operator==(const TrackLayout& lhs, const TrackLayout& rhs) {
    return lhs.trackId == rhs.trackId &&
           lhs.mainLaneHeight == rhs.mainLaneHeight &&
           lhs.takesLaneHeights == rhs.takesLaneHeights &&
           lhs.totalHeight == rhs.totalHeight &&
           lhs.isTakesExpanded == rhs.isTakesExpanded &&
           lhs.subLanes == rhs.subLanes;
}

std::vector<TrackLayout> buildTrackLayouts(
    const std::vector<bridge::TrackUIState>& tracks,
    const std::unordered_map<uint64_t, int>& trackHeights,
    const std::unordered_map<uint64_t, uint32_t>& autoSubLaneHeights,
    const std::map<std::pair<uint64_t, uint32_t>, int>& takesLaneHeights,
    double defaultTrackHeight);

} // namespace presentation::views
