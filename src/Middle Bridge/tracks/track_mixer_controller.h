#pragma once
#include "tracks/track_controller_context.h"
#include "common/system_primitives.h"
#include <vector>

namespace bridge {

class TrackMixerController {
public:
    explicit TrackMixerController(TrackControllerContext context);

    void setFaderGain(TrackID trackId, float gainLinear);
    void setPan(TrackID trackId, float panPosition);
    void setMute(TrackID trackId, bool mute);
    void setSolo(TrackID trackId, bool solo);
    void setRecordArmed(TrackID trackId, bool armed);
    void setInputMonitoring(TrackID trackId, bool enabled);

private:
    TrackControllerContext ctx_;
};

} // namespace bridge
