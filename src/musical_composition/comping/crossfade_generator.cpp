#include "crossfade_generator.h"
#include "musical_composition/playlist/iplaylist.h"

namespace composition {

void CrossfadeGenerator::applyEqualPowerFade(
    RegionID leftRegion, 
    RegionID rightRegion, 
    uint32_t overlapSamples,
    IPlaylist* playlist
) {
    if (!playlist) {
        return;
    }

    if (leftRegion.isValid()) {
        playlist->setFades(leftRegion, 0, overlapSamples);
    }
    if (rightRegion.isValid()) {
        playlist->setFades(rightRegion, overlapSamples, 0);
    }
}

} // namespace composition
