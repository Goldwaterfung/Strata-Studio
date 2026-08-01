// src/Middle Bridge/recording/irecording_controller.h
#pragma once

#include "common/system_primitives.h"
#include "Core infrastructure/bridges/spsc_queue.h"
#include <string>
#include <memory>

namespace MediaManagement { class IDiskWriterService; class ICodecFactory; }

namespace bridge {

class IRecordingController {
public:
    virtual ~IRecordingController() = default;

    virtual void setDiskWriterService(std::shared_ptr<MediaManagement::IDiskWriterService> service) = 0;
    virtual void setTempRecordingDirectory(const std::string& path) = 0;
    virtual void setCodecFactory(MediaManagement::ICodecFactory* factory) = 0;

    virtual std::shared_ptr<Layer2::SPSCQueue<float, 524288>> prepareTrackForRecording(TrackID trackId, bool armed) = 0;

    struct RecordingPassCompleteEvent {
        std::string filePath;
        TrackID trackId;
        NodeID inputNodeId; // For latency compensation
        uint64_t startSample;
        uint64_t endSample;
        bool isLoopRecording;
    };

    virtual void onRecordingPassComplete(const RecordingPassCompleteEvent& event) = 0;
    
    virtual void onTransportRecordingStarted(uint64_t startSample) = 0;
    virtual void onTransportRecordingStopped(uint64_t endSample) = 0;

    struct ActiveRecordingInfo {
        TrackID trackId;
        uint64_t startSample;
        uint64_t currentSample;
        const float* livePeaks;
        uint32_t numPeaks;
    };
    virtual uint32_t getActiveRecordings(ActiveRecordingInfo* out, uint32_t maxCount) = 0;
};

} // namespace bridge
