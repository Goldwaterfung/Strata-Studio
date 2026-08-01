// src/Middle Bridge/arrangement_controller.h
#pragma once

#include "Middle Bridge/timeline/iarrangement_controller.h"
#include "project/isession_manager.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "musical_composition/region_manager/iaudio_region_source_manager.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "musical_composition/playlist/iplaylist.h"
#include "Media management/registry/imedia_registry.h"
#include "Media management/waveforms/iwaveform_renderer.h"
#include <vector>

#include <unordered_map>
#include <string>

namespace Layer1 {
    class IFileSystem;
}

namespace bridge {

class IRecordingController;
using composition::RegionID;
using composition::SourceID;

class ArrangementController : public IArrangementController, public ISessionChangeListener {
public:
    ArrangementController(
        ISessionManager* sessionManager,
        Layer2::IStringRegistry* stringRegistry,
        MediaManagement::IMediaRegistry* mediaRegistry = nullptr,
        MediaManagement::IWaveformRenderer* waveformRenderer = nullptr,
        Layer1::IFileSystem* fs = nullptr
    );

    ~ArrangementController() override;

    void setRecordingController(IRecordingController* rc) { recordingController_ = rc; }

    // --- Clip / Region Mutations (Undoable via Layer 5) ---
    RegionID importAudioClip(TrackID targetTrack, const char* filePath, uint64_t startFrame) override;
    RegionID insertMidiClip(TrackID targetTrack, uint64_t startFrame, uint64_t durationFrames) override;
    RegionID insertAutomationClip(
        TrackID targetTrack,
        NodeID dspNode,
        uint32_t parameterIndex,
        uint64_t startFrame,
        uint64_t durationFrames
    ) override;
    void deleteRegion(RegionID regionId) override;
    void splitRegion(RegionID regionId, uint64_t splitFrame) override;
    RegionID moveRegion(RegionID regionId, TrackID destTrack, int64_t newStartFrame, uint32_t destLayer = 0xFFFFFFFF) override;
    void trimRegion(RegionID regionId, uint64_t newPosition, uint64_t newSourceStart, uint64_t newDuration) override;
    bool getVisualRegion(RegionID regionId, VisualRegion& outRegion) const override;

    // --- Per-Clip Properties ---
    void setRegionGain(RegionID id, float gainLinear) override;
    void setRegionFades(RegionID id, uint32_t fadeInFrames, uint32_t fadeOutFrames) override;
    void setRegionMuted(RegionID id, bool muted) override;
    void setRegionWarpMode(RegionID id, WarpMode mode) override;
    void setRegionPlaybackRatio(RegionID id, float ratio) override;
    void setRegionSourceBpm(RegionID id, float bpm) override;

    // --- Region Metadata ---
    void updateRegionMetadata(RegionID regionId, const char* name, const char* comment, uint32_t colorARGB) override;
    void initializeRegionMetadata(RegionID regionId, const char* defaultName, uint32_t colorARGB) override;

    // --- Merge ---
    void mergePatternClips(TrackID trackId,
                           uint64_t startFrame, uint64_t endFrame) override;

    // --- Takes ---
    void swapTakeLayer(TrackID trackId, uint32_t laneIndex, uint64_t startFrame, uint64_t length) override;

    // --- Minor Gap additions ---
    void autoNameClips(TrackID trackId) override;
    bool undo() override;
    bool redo() override;
    void consolidateTrack(TrackID trackId, uint64_t startFrame, uint64_t endFrame) override;
    uint64_t getArrangementLength() const override;

    // --- High-Performance Viewport Queries (Viewport Culling support) ---
    uint32_t getRegionsInViewport(
        uint64_t viewStartFrame,
        uint64_t viewEndFrame,
        VisualRegion* outRegions,
        uint32_t maxCount
    ) const override;

    uint32_t getActiveRecordings(VisualActiveRecording* outRecordings, uint32_t maxCount) override;

    // --- ISessionChangeListener ---
    void onSessionChanging() override { regionsScratch_.clear(); }
    void onSessionChanged(composition::IProjectSession* session) override;

private:
    composition::ITrackManager* getTrackManager() const;
    composition::IAudioRegionSourceManager* getSourceManager() const;

    ISessionManager* sessionManager_ = nullptr;
    IRecordingController* recordingController_ = nullptr;
    Layer2::IStringRegistry* stringRegistry_ = nullptr;
    MediaManagement::IMediaRegistry* mediaRegistry_ = nullptr;
    MediaManagement::IWaveformRenderer* waveformRenderer_ = nullptr;
    Layer1::IFileSystem* fs_ = nullptr;

    mutable std::vector<composition::IPlaylist::RegionInfo> regionsScratch_;

    composition::IPlaylist* findPlaylistForRegion(RegionID regionId, composition::IPlaylist::RegionInfo& outInfo, TrackID& outTrackId) const;
};

} // namespace bridge
