// src/musical_composition/recording/take_recording_intake.h
#pragma once

#include "itake_recording_intake.h"

namespace composition {
class IProjectSession;
}
namespace Layer2 {
class IStringRegistry;
}

namespace composition {

class TakeRecordingIntake : public ITakeRecordingIntake {
public:
    explicit TakeRecordingIntake(Layer2::IStringRegistry* stringRegistry);
    ~TakeRecordingIntake() override = default;

    std::vector<RegionID> ingestTake(const RawTake& take, 
                                     uint64_t totalLatencySamples, 
                                     const RecordingConfig& config,
                                     IPlaylist* targetPlaylist,
                                     IAudioRegionSourceManager* sourceManager) override;

private:
    Layer2::IStringRegistry* stringRegistry_ = nullptr;
};

} // namespace composition
