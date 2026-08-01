#pragma once
#include "musical_composition/command_history/delta_primitives.h"
#include "musical_composition/musical_primitives.h"

namespace composition {

namespace PlaylistOps {
    constexpr uint32_t ADD_REGION    = 0;
    constexpr uint32_t REMOVE_REGION = 1;
    constexpr uint32_t MOVE_REGION   = 2;
    constexpr uint32_t TRIM_REGION   = 3;
    constexpr uint32_t SPLIT_REGION  = 4;
    constexpr uint32_t FADES_REGION  = 5;
    constexpr uint32_t WARP_REGION   = 6;
    constexpr uint32_t MUTE_REGION   = 7;
    constexpr uint32_t GAIN_REGION   = 8;
}

// Payload structs that get memcpy'd into ProjectDelta's 256-byte buffer
struct AddRegionPayload {
    RegionID regionId;
    TimelineRegion region;
    uint32_t layer;
};

struct FadesRegionPayload {
    uint32_t fadeInSamples;
    uint32_t fadeOutSamples;
};

struct MoveRegionPayload {
    uint64_t newPositionSample;
    uint32_t newLayer;
};

struct TrimRegionPayload {
    uint64_t newPositionSample;
    uint64_t newSourceStartSample;
    uint64_t newSourceLength;
};

struct SplitRegionPayload {
    uint64_t splitPointSample;
    uint64_t sourceOffsetSample;
    RegionID leftRegionId;
    RegionID rightRegionId;
};

struct WarpRegionPayload {
    WarpMode warpMode;
    float playbackRatio;
    float sourceBpm;
};

struct MuteRegionPayload {
    bool oldMuted;
    bool newMuted;
};

struct GainRegionPayload {
    float oldGain;
    float newGain;
};

static_assert(sizeof(AddRegionPayload) <= 256, "Add payload too large");
static_assert(sizeof(MoveRegionPayload) <= 256, "Move payload too large");
static_assert(sizeof(TrimRegionPayload) <= 256, "Trim payload too large");
static_assert(sizeof(SplitRegionPayload) <= 256, "Split payload too large");
static_assert(sizeof(MuteRegionPayload) <= 256, "Mute payload too large");
static_assert(sizeof(GainRegionPayload) <= 256, "Gain payload too large");

} // namespace composition
