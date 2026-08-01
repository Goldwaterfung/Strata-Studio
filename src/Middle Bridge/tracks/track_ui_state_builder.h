#pragma once
#include "tracks/track_controller_context.h"
#include "common/system_primitives.h"
#include "tracks/itrack_controller.h"
#include <unordered_map>
#include <vector>

namespace bridge {

class TrackUIStateBuilder {
public:
    explicit TrackUIStateBuilder(TrackControllerContext context);

    std::vector<ParameterDescriptorCacheItem> getCachedParameters(TrackID trackId) const;
    uint32_t getTrackCount() const;
    TrackUIState getTrackState(TrackID trackId) const;
    std::vector<TrackUIState> getAllTracks() const;
    TrackDynamicState getDynamicState(NodeID channelStripNode) const;
    std::vector<PluginDescriptor> getAvailablePlugins() const;

    void setAutomationExpanded(TrackID id, bool expanded);
    void setAutomationSubLaneExpanded(TrackID id, uint32_t subLaneIndex, bool expanded);
    void setAutomationSubLaneHeight(TrackID id, uint32_t subLaneIndex, uint32_t heightPx);
    void setTrackSelected(TrackID id, bool selected);
    void clearTrackSelection();

    void clearParameterCache() {
        trackParameterCache_.clear();
    }
    
    void initializeTrackParameterCache(TrackID trackId, composition::ITrackManager* trackManager);

private:
    TrackUIState getTrackStateInternal(TrackID trackId) const;

    TrackControllerContext ctx_;


    mutable std::unordered_map<uint64_t, bool> automationExpanded_;
    mutable std::unordered_map<uint64_t, std::vector<bool>> subLanesExpanded_;
    mutable std::unordered_map<uint64_t, std::vector<uint32_t>> subLaneHeights_;
    mutable std::unordered_map<uint64_t, std::vector<ParameterDescriptorCacheItem>> trackParameterCache_;
};

} // namespace bridge
