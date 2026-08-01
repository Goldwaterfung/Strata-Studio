#pragma once
#include "iplaylist.h"
#include "musical_composition/command_history/delta_primitives.h"
#include <vector>
#include <array>
#include <unordered_map>

namespace composition {
class ICommandHistory;
class IAudioRegionSourceManager;

class PlaylistImpl : public IPlaylist {
public:
    PlaylistImpl(TrackID trackId, ICommandHistory* history, IAudioRegionSourceManager* sourceManager = nullptr);

    TrackID getTrackId() const { return trackId_; }

    RegionID addRegion(const TimelineRegion& region, LayerIndex layer) override;
    void removeRegion(RegionID id) override;
    
    void moveRegion(RegionID id, uint64_t newPosition, LayerIndex newLayer) override;
    void trimRegion(RegionID id, uint64_t newPosition, uint64_t newSourceStart, uint64_t newSourceLength) override;
    RegionID splitRegion(RegionID id, uint64_t splitPointSample, uint64_t sourceOffsetSample = 0) override;
    void setProjectSampleRate(uint32_t sampleRate) override;
    void setFades(RegionID id, uint32_t fadeInSamples, uint32_t fadeOutSamples) override;
    void setRegionMuted(RegionID id, bool muted) override;
    void setRegionGain(RegionID id, float gainDb) override;
    
    void setWarpMode(RegionID id, WarpMode mode) override;
    void setPlaybackRatio(RegionID id, float ratio) override;
    void setSourceBpm(RegionID id, float bpm) override;

    uint32_t getAllRegions(RegionInfo* outRegions, uint32_t maxRegions) const override;
    uint32_t getRegionsAt(uint64_t samplePos, TimelineRegion* outRegions, uint32_t maxRegions) const override;
    uint32_t getMaxLayer() const override;

    void applyDelta(const ProjectDelta& delta, bool isUndo);
    void copyFrom(const PlaylistImpl* other);

    // Deserialization restore method
    void restoreRegion(RegionID id, const TimelineRegion& region, LayerIndex layer);

    // RegionEntry is public so TrackManagerImpl can iterate for cache recalculation.
    struct RegionEntry {
        RegionID regionId;
        TimelineRegion region;
        LayerIndex layer;
    };

    // Expose the mutable region list for tempo-map-driven cache recalculation.
    // Only for use by TrackManagerImpl::recalculateTimeCaches — NOT for RT paths.
    std::vector<RegionEntry>& getMutableRegions() { return regions_; }
    const std::vector<RegionEntry>& getRawRegions() const { return regions_; }

private:
    TrackID trackId_;
    ICommandHistory* history_;
    IAudioRegionSourceManager* sourceManager_;
    uint32_t projectSampleRate_ = 44100;

    std::vector<RegionEntry> regions_;

    RegionID generateNextId();
    RegionID addRegionInternal(const TimelineRegion& region, LayerIndex layer, RegionID forcedId, bool pushDelta);
    void removeRegionInternal(RegionID id, bool pushDelta);
    RegionID splitRegionInternal(RegionID id, uint64_t splitPointSample, uint64_t sourceOffsetSample, RegionID forcedRightId, bool pushDelta);
    LayerIndex findFreeLayer(uint64_t start, uint64_t length) const;
    bool isLayerFree(LayerIndex layer, uint64_t start, uint64_t length) const;
    void shiftOverlappingLayersDown(uint64_t start, uint64_t length, LayerIndex startingLayer);
};

} // namespace composition
