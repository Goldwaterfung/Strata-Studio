// src/Middle Bridge/recording/recording_controller.h
#pragma once

#include "irecording_controller.h"
#include "project/isession_manager.h"
#include "Core audio engine/engine/iaudio_engine.h"
#include "Media management/recording/idisk_writer_service.h"
#include "Media management/codecs/icodec_factory.h"
#include "Core infrastructure/bridges/imutation_bridge.h"
#include "common/system_primitives.h"

#include <mutex>
#include <unordered_map>
#include <string>
#include <memory>
#include <vector>

#include "musical_composition/recording/itake_recording_intake.h"

namespace MediaManagement { class IMediaIntakePipeline; }

namespace bridge {

class RecordingController : public IRecordingController {
public:
    RecordingController() = default;
    ~RecordingController() override = default;

    void setAudioEngine(Layer3::IAudioEngine* engine) { audioEngine_ = engine; }
    void setSessionManager(ISessionManager* sessionManager) { sessionManager_ = sessionManager; }
    void setMutationBridge(Layer2::IMutationBridge* bridge) { mutationBridge_ = bridge; }
    void setTakeRecordingIntake(composition::ITakeRecordingIntake* intake) { intake_ = intake; }
    void setIntakePipeline(MediaManagement::IMediaIntakePipeline* pipeline) { intakePipeline_ = pipeline; }

    void setDiskWriterService(std::shared_ptr<MediaManagement::IDiskWriterService> service) override;
    void setTempRecordingDirectory(const std::string& path) override;
    void setCodecFactory(MediaManagement::ICodecFactory* factory) override;
    
    std::shared_ptr<Layer2::SPSCQueue<float, 524288>> prepareTrackForRecording(TrackID trackId, bool armed) override;
    
    void onTransportRecordingStarted(uint64_t startSample) override;
    void onTransportRecordingStopped(uint64_t endSample) override;
    void onRecordingPassComplete(const RecordingPassCompleteEvent& event) override;
    uint32_t getActiveRecordings(ActiveRecordingInfo* out, uint32_t maxCount) override;

private:
    Layer3::IAudioEngine* audioEngine_ = nullptr;
    ISessionManager* sessionManager_ = nullptr;
    Layer2::IMutationBridge* mutationBridge_ = nullptr;
    composition::ITakeRecordingIntake* intake_ = nullptr;
    MediaManagement::IMediaIntakePipeline* intakePipeline_ = nullptr;

    std::shared_ptr<MediaManagement::IDiskWriterService> diskWriterService_;
    std::string tempRecordingDirectory_;
    MediaManagement::ICodecFactory* codecFactory_ = nullptr;

    mutable std::recursive_mutex mutex_;

    struct ActiveRecording {
        std::string filePath;
        NodeID inputNodeId;
        uint64_t startSample = 0;
        std::shared_ptr<Layer2::SPSCQueue<float, 16384>> peakQueue;
        std::vector<float> livePeaks;
    };
    std::unordered_map<uint32_t, ActiveRecording> activeRecordings_;
    std::unordered_map<uint32_t, uint32_t> trackTakeCounters_;
};

} // namespace bridge
