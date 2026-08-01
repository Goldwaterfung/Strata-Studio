#pragma once
#include "tracks/track_controller_context.h"
#include "common/system_primitives.h"
#include "tracks/itrack_controller.h" // For TrackUIState, etc. (Or just composition::TrackType)
#include <vector>

namespace bridge {

class TrackLifecycleController {
public:
    explicit TrackLifecycleController(TrackControllerContext context);

    TrackID addAudioTrack(const char* name, uint32_t channels, uint32_t colorARGB);
    TrackID addInstrumentTrack(const char* name, uint32_t colorARGB);
    TrackID addAuxTrack(const char* name, uint32_t colorARGB);
    TrackID addFolderTrack(const char* name, uint32_t colorARGB);
    void removeTrack(TrackID trackId);
    void renameTrack(TrackID trackId, const char* name);
    void setTrackColor(TrackID trackId, uint32_t colorARGB);
    void moveTrack(TrackID trackId, uint32_t newPositionIndex, TrackID newParentFolderId);
    void setTrackParentFolder(TrackID childTrackId, TrackID parentFolderId);
    void setTrackMode(TrackID trackId, composition::TrackType mode);
    TrackID cloneTrack(TrackID sourceId);
    void muteAllClips(TrackID trackId, bool mute);

    // Called by other sub-controllers or session change
    std::string getUniqueTrackName(const std::string& baseName, TrackID excludeTrackId) const;
    static void eagerlyCreateStandardAutomationLanes(
        TrackID trackId,
        composition::ITrackManager* trackManager,
        Layer2::IStringRegistry* stringRegistry
    );
    void initializeTrackParameterCache(TrackID trackId, composition::ITrackManager* trackManager);
    
    // Callback to integrate with UI State builder
    void setParameterCacheCallback(std::function<void(TrackID, composition::ITrackManager*)> cb) {
        paramCacheCb_ = std::move(cb);
    }

private:
    TrackControllerContext ctx_;
    std::function<void(TrackID, composition::ITrackManager*)> paramCacheCb_;
};

} // namespace bridge
