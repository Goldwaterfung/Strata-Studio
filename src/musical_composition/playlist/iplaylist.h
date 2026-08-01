#pragma once
#include "musical_composition/musical_primitives.h"

namespace composition {

class IPlaylist {
public:
    using LayerIndex = uint32_t;
    constexpr static LayerIndex AUTO_LAYER = UINT32_MAX; 
    constexpr static uint32_t MAX_LAYERS = 32;

    virtual ~IPlaylist() = default;

    /**
     * @brief Add a region to the timeline. (Non-RT thread only)
     * @param region The timeline region primitive
     * @param layer The target layer (AUTO_LAYER finds lowest free layer)
     * @return A stable generation-counted RegionID
     */
    virtual RegionID addRegion(const TimelineRegion& region, LayerIndex layer = AUTO_LAYER) = 0;
    virtual void removeRegion(RegionID id) = 0;

    virtual void moveRegion(RegionID id, uint64_t newPosition, LayerIndex newLayer) = 0;
    virtual void trimRegion(RegionID id, uint64_t newPosition, uint64_t newSourceStart, uint64_t newSourceLength) = 0;
    
    /**
     * @brief Split a region into two at a timeline position.
     * @return The ID of the newly created right-side region.
     */
    virtual RegionID splitRegion(RegionID id, uint64_t splitPointSample, uint64_t sourceOffsetSample = 0) = 0;
    
    virtual void setProjectSampleRate(uint32_t sampleRate) = 0;
    
    /**
     * @brief Update fade durations for a region.
     */
    virtual void setFades(RegionID id, uint32_t fadeInSamples, uint32_t fadeOutSamples) = 0;
    virtual void setRegionMuted(RegionID id, bool muted) = 0;
    virtual void setRegionGain(RegionID id, float gainDb) = 0;

    /**
     * @brief Update warping mode and ratio for a region. (Phase 4)
     */
    virtual void setWarpMode(RegionID id, WarpMode mode) = 0;
    virtual void setPlaybackRatio(RegionID id, float ratio) = 0;
    virtual void setSourceBpm(RegionID id, float bpm) = 0;

    /**
     * @brief RT-Safe query for the audio engine to determine what to play.
     * @thread_safety RT-Safe but NOT thread-safe for concurrent mutation.
     * Assumes no mutations (add/remove/split) occur while this is being called.
     * @param samplePos Timeline position being processed
     * @param outRegions Caller-provided array to fill
     * @param maxRegions Maximum capacity of the caller array
     * @return Number of regions actually written to the array
     */
    struct RegionInfo {
        RegionID id;
        TimelineRegion region;
        LayerIndex layer;
    };

    virtual uint32_t getAllRegions(
        RegionInfo* outRegions,
        uint32_t maxRegions
    ) const = 0;

    /**
     * @brief Swaps all regions (or a specific time slice) between sourceLayer and layer 0.
     * If length > 0, it first slices regions at the time boundaries.
     */

    virtual uint32_t getMaxLayer() const = 0;

    virtual uint32_t getRegionsAt(
        uint64_t samplePos,
        TimelineRegion* outRegions,
        uint32_t maxRegions
    ) const = 0;
};

} // namespace composition
