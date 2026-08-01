#pragma once
#include "common/system_primitives.h"
#include "musical_composition/musical_primitives.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include <vector>

namespace bridge {

using composition::RegionID;

struct VisualRegion {
    RegionID id;
    TrackID trackId;
    char name[MAX_NAME_LENGTH];
    char comment[MAX_COMMENT_LENGTH];
    bool hasCustomComment;
    uint64_t startFrame;
    uint64_t durationFrames;
    uint64_t fileOffsetFrames;
    bool isSelected;
    bool isMuted;
    float    gainLinear;         ///< Per-clip gain (1.0 = unity)
    uint32_t fadeInFrames;       ///< 0 = no fade
    uint32_t fadeOutFrames;      ///< 0 = no fade
    uint64_t mediaId;            ///< Layer 6 media handle for waveform cache lookup
    composition::RegionType clipType; ///< Audio / MIDI / Automation discriminator
    uint32_t colorARGB;              ///< Clip/track colour for rendering
    WarpMode warpMode;           ///< Clip warping mode
    float    playbackRatio;      ///< Stretch ratio (1.0 = normal)
    float    sourceBpm;          ///< Base BPM of source file
    double   timelineToSourceRatio; ///< Maps timeline project frames to source file frames
    uint64_t sourceLengthFrames; ///< Total length of source in project frames
    NodeID   automationTargetNodeId;     ///< Target node for automation
    uint32_t automationParameterIndex;   ///< Target parameter index
    uint32_t layerIndex;                 ///< Playlist layer index
};

/**
 * @brief Controller interface for arranging clips, regions and managing the arrangement canvas
 */
class IArrangementController {
public:
    virtual ~IArrangementController() = default;

    virtual RegionID importAudioClip(TrackID targetTrack, const char* filePath, uint64_t startFrame) = 0;
    virtual RegionID insertMidiClip(TrackID targetTrack, uint64_t startFrame, uint64_t durationFrames) = 0;
    virtual RegionID insertAutomationClip(
        TrackID targetTrack,
        NodeID dspNode,
        uint32_t parameterIndex,
        uint64_t startFrame,
        uint64_t durationFrames
    ) = 0;
    virtual void deleteRegion(RegionID regionId) = 0;
    virtual void splitRegion(RegionID regionId, uint64_t splitFrame) = 0;
    virtual RegionID moveRegion(RegionID regionId, TrackID destTrack, int64_t newStartFrame, uint32_t destLayer = 0xFFFFFFFF) = 0;
    virtual void trimRegion(RegionID regionId, uint64_t newPosition, uint64_t newSourceStart, uint64_t newDuration) = 0;
    virtual bool getVisualRegion(RegionID regionId, VisualRegion& outRegion) const = 0;

    // --- Per-Clip Properties ---
    virtual void setRegionGain(RegionID id, float gainLinear) = 0;
    virtual void setRegionFades(RegionID id, uint32_t fadeInFrames, uint32_t fadeOutFrames) = 0;
    virtual void setRegionMuted(RegionID id, bool muted) = 0;
    virtual void setRegionWarpMode(RegionID id, WarpMode mode) = 0;
    virtual void setRegionPlaybackRatio(RegionID id, float ratio) = 0;
    virtual void setRegionSourceBpm(RegionID id, float bpm) = 0;

    // --- Region Metadata ---
    virtual void updateRegionMetadata(RegionID regionId, const char* name, const char* comment, uint32_t colorARGB) = 0;
    virtual void initializeRegionMetadata(RegionID regionId, const char* defaultName, uint32_t colorARGB) = 0;

    // --- Merge ---
    virtual void mergePatternClips(TrackID trackId,
                                   uint64_t startFrame, uint64_t endFrame) = 0;

    // --- Takes ---
    virtual void swapTakeLayer(TrackID trackId, uint32_t laneIndex, uint64_t startFrame, uint64_t length) = 0;

    // --- Minor Gap additions ---
    virtual void autoNameClips(TrackID trackId) = 0;
    virtual bool undo() = 0;
    virtual bool redo() = 0;
    virtual void consolidateTrack(TrackID trackId, uint64_t startFrame, uint64_t endFrame) = 0;
    virtual uint64_t getArrangementLength() const = 0;

    // --- High-Performance Viewport Queries (Viewport Culling support) ---
    virtual uint32_t getRegionsInViewport(
        uint64_t viewStartFrame,
        uint64_t viewEndFrame,
        VisualRegion* outRegions,
        uint32_t maxCount
    ) const = 0;

    struct VisualActiveRecording {
        TrackID trackId;
        uint64_t startFrame;
        uint64_t currentFrame;
        const float* livePeaks;
        uint32_t numPeaks;
    };
    virtual uint32_t getActiveRecordings(VisualActiveRecording* outRecordings, uint32_t maxCount) = 0;
};

} // namespace bridge
