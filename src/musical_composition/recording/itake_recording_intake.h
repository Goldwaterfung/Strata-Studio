// src/musical_composition/recording/itake_recording_intake.h
#pragma once

#include "common/system_primitives.h"
#include "musical_composition/musical_primitives.h"
#include <string>
#include <vector>

namespace composition {

struct RecordingConfig {
    bool isLoopRecording = false;
    bool isOverdub = false;
    uint64_t loopStart = 0;
    uint64_t loopEnd = 0;
};

struct RawTake {
    std::string filePath;
    TrackID trackId;
    NodeID inputNodeId;
    uint64_t hardwareStartSample = 0;
    uint64_t hardwareEndSample = 0;
    uint64_t mediaId = 0;
    uint32_t sampleRate = 44100;
    uint32_t channelCount = 2;
};

class ITakeRecordingIntake {
public:
    virtual ~ITakeRecordingIntake() = default;

    /**
     * @brief Ingests a raw audio take into the timeline.
     * @param take Metadata about the recorded asset from the Media layer.
     * @param totalLatencySamples Total round-trip and PDC latency to compensate for.
     * @param config Context of the recording (looping, overdub, etc).
     * @return std::vector<RegionID> The timeline regions created from the take.
     */
    virtual std::vector<RegionID> ingestTake(const RawTake& take, 
                                             uint64_t totalLatencySamples, 
                                             const RecordingConfig& config,
                                             class IPlaylist* targetPlaylist,
                                             class IAudioRegionSourceManager* sourceManager) = 0;
};

} // namespace composition
