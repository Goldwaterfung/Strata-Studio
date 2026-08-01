#pragma once
#include "musical_composition/musical_primitives.h"

namespace composition {

class IPlaylist;

struct CrossfadeGenerator {
    /**
     * @brief Computes equal-power crossfade curves for two overlapping regions.
     * @param leftRegion The outgoing region
     * @param rightRegion The incoming region
     * @param overlapSamples Duration of the crossfade window
     * @param playlist Target playlist to receive fade configuration
     */
    static void applyEqualPowerFade(
        RegionID leftRegion, 
        RegionID rightRegion, 
        uint32_t overlapSamples,
        IPlaylist* playlist
    );
};

} // namespace composition
