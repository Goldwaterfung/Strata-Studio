// src/Middle Bridge/recording/recording_controller.cpp
#include "recording_controller.h"
#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/command_history/icommand_history.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "musical_composition/region_manager/iaudio_region_source_manager.h"
#include "musical_composition/interfaces/iregion_metadata_manager.h"
#include "DSP nodes/audio_input/audio_input_node.h"
#include "Media management/intake/imedia_intake_pipeline.h"

#include <chrono>
#include <filesystem>

namespace bridge {

void RecordingController::setDiskWriterService(std::shared_ptr<MediaManagement::IDiskWriterService> service) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    diskWriterService_ = std::move(service);
}

void RecordingController::setTempRecordingDirectory(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    tempRecordingDirectory_ = path;
}

void RecordingController::setCodecFactory(MediaManagement::ICodecFactory* factory) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    codecFactory_ = factory;
}

std::shared_ptr<Layer2::SPSCQueue<float, 524288>> RecordingController::prepareTrackForRecording(TrackID trackId, bool armed) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    if (!diskWriterService_) return nullptr;

    if (!armed) {
        diskWriterService_->unregisterTrack(trackId.id);
        activeRecordings_.erase(trackId.id);
        return nullptr;
    }

    if (!codecFactory_ || !sessionManager_) return nullptr;
    
    auto* activeSession = sessionManager_->getActiveSession();
    if (!activeSession) return nullptr;

    auto* trackManager = activeSession->getTrackManager();
    if (!trackManager) return nullptr;

    composition::TrackCreateInfo info{};
    trackManager->getTrackInfo(trackId, info);

    std::string trackNameStr = "";
    if (trackNameStr.empty()) {
        trackNameStr = "Track " + std::to_string(trackId.id);
    }

    uint32_t numChannels = 2; // default
    auto desc = trackManager->getPipelineDescriptor(trackId);
    if (desc.audioInputNode.isValid()) {
        auto* state = DSP::AudioInputFactory::getRegistry().get(desc.audioInputNode);
        if (state) {
            numChannels = state->buffers[0].numChannels;
        }
    }

    uint32_t takeNum = ++trackTakeCounters_[trackId.id];
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::string filePath = tempRecordingDirectory_ + trackNameStr + "_" + std::to_string(timestamp) + "_Take_" + std::to_string(takeNum) + ".wav";

    try {
        std::filesystem::create_directories(tempRecordingDirectory_);
    } catch (...) {
        return nullptr;
    }

    uint32_t sampleRate = audioEngine_ ? static_cast<uint32_t>(audioEngine_->getSampleRate()) : 44100;

    auto writer = codecFactory_->createWriter(filePath, sampleRate, static_cast<uint16_t>(numChannels), 32);
    if (!writer) return nullptr;

    ActiveRecording rec;
    rec.filePath = filePath;
    rec.inputNodeId = desc.audioInputNode;
    rec.startSample = 0;

    auto pair = diskWriterService_->registerTrack(trackId.id, std::shared_ptr<MediaManagement::ICodecWriter>(std::move(writer)), numChannels);
    rec.peakQueue = pair.second;
    activeRecordings_[trackId.id] = std::move(rec);

    return pair.first;
}

void RecordingController::onTransportRecordingStarted(uint64_t startSample) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    // Auto re-arm tracks that are record-armed but not currently in activeRecordings_
    if (sessionManager_) {
        if (auto* activeSession = sessionManager_->getActiveSession()) {
            if (auto* trackManager = activeSession->getTrackManager()) {
                auto trackIds = trackManager->getAllTrackIDs();
                for (auto trackId : trackIds) {
                    composition::TrackCreateInfo info{};
                    if (trackManager->getTrackInfo(trackId, info)) {
                        if (info.isRecordArmed && activeRecordings_.find(trackId.id) == activeRecordings_.end()) {
                            auto queue = prepareTrackForRecording(trackId, true);
                            if (queue && mutationBridge_) {
                                auto desc = trackManager->getPipelineDescriptor(trackId);
                                if (desc.audioInputNode.isValid()) {
                                    SystemMutation mut{};
                                    mut.type = Layer2::MutationType::RECORD_ARM_SET;
                                    mut.targetId = desc.audioInputNode;
                                    mut.priority = 0;
                                    mut.record.isArmed = true;
                                    mut.record.recordingQueue = queue.get();
                                    mutationBridge_->pushMutation(mut);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    for (auto& pair : activeRecordings_) {
        pair.second.startSample = startSample;
    }
}

void RecordingController::onTransportRecordingStopped(uint64_t endSample) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    composition::ICommandHistory* history = nullptr;
    if (sessionManager_) {
        if (auto* session = sessionManager_->getActiveSession()) {
            history = session->getCommandHistory();
        }
    }
    
    if (history) {
        history->beginCompound();
    }
    
    for (auto it = activeRecordings_.begin(); it != activeRecordings_.end(); ) {
        uint32_t trackId = it->first;
        auto& rec = it->second;

        if (diskWriterService_) {
            diskWriterService_->unregisterTrack(trackId); // Flushes and closes file
        }

        RecordingPassCompleteEvent event{};
        event.filePath = rec.filePath;
        event.trackId = TrackID{trackId, 1};
        event.inputNodeId = rec.inputNodeId;
        event.startSample = rec.startSample;
        event.endSample = endSample;
        event.isLoopRecording = false;

        onRecordingPassComplete(event);

        it = activeRecordings_.erase(it);
    }
    
    if (history) {
        history->endCompound();
    }
}

void RecordingController::onRecordingPassComplete(const RecordingPassCompleteEvent& event) {
    if (!intakePipeline_) return;

    // Delegate to intake pipeline
    MediaManagement::ImportOptions options = MediaManagement::ImportOptions::defaults();
    auto result = intakePipeline_->processAsset(event.filePath, options, nullptr);

    if (!result.success) return;

    // Calculate Latency Compensation (PDC + RTL)
    uint64_t totalDelay = 0;
    if (audioEngine_) {
        totalDelay = audioEngine_->getTotalRTLSamples() + audioEngine_->getNodePDCDelaySamples(event.inputNodeId);
    }
    
    if (intake_) {
        uint64_t actualStart = event.startSample;
        if (sessionManager_) {
            if (auto* session = sessionManager_->getActiveSession()) {
                if (auto* trackManager = session->getTrackManager()) {
                    if (auto* atomicStart = trackManager->getRecordingStartSample(event.trackId)) {
                        uint64_t val = atomicStart->load();
                        if (val != std::numeric_limits<uint64_t>::max()) {
                            actualStart = val;
                        }
                        atomicStart->store(std::numeric_limits<uint64_t>::max());
                    }
                }
            }
        }
        
        composition::RawTake take;
        take.filePath = event.filePath;
        take.trackId = event.trackId;
        take.inputNodeId = event.inputNodeId;
        take.hardwareStartSample = actualStart;
        take.hardwareEndSample = event.endSample;
        take.mediaId = result.mediaId.toRaw();
        take.sampleRate = audioEngine_ ? static_cast<uint32_t>(audioEngine_->getSampleRate()) : 44100;
        take.channelCount = 2; // Can hardcode for now, or extract from somewhere
        
        composition::RecordingConfig config;
        config.isLoopRecording = event.isLoopRecording;
        config.isOverdub = false;
        config.loopStart = 0;
        config.loopEnd = 0;
        
        if (sessionManager_) {
            if (auto* session = sessionManager_->getActiveSession()) {
                auto* trackManager = session->getTrackManager();
                auto* playlist = trackManager ? trackManager->getPlaylist(take.trackId) : nullptr;
                auto* sourceManager = session->getRegionSourceManager();
                if (playlist && sourceManager) {
                    auto createdRegions = intake_->ingestTake(take, totalDelay, config, playlist, sourceManager);
                    if (auto* metaMgr = session->getRegionMetadataManager()) {
                        composition::TrackCreateInfo trackInfo{};
                        uint32_t trackColor = 0xFF8B5CF6;
                        if (trackManager->getTrackInfo(take.trackId, trackInfo)) {
                            trackColor = trackInfo.colorARGB;
                        }

                        std::string clipName = "Audio Take";
                        if (!take.filePath.empty()) {
                            std::size_t lastSlash = take.filePath.find_last_of("/\\");
                            if (lastSlash != std::string::npos) {
                                clipName = take.filePath.substr(lastSlash + 1);
                            } else {
                                clipName = take.filePath;
                            }
                        }

                        for (const auto& regId : createdRegions) {
                            composition::RegionMetadata meta{};
                            std::strncpy(meta.name, clipName.c_str(), MAX_NAME_LENGTH - 1);
                            meta.name[MAX_NAME_LENGTH - 1] = '\0';
                            meta.comment[0] = '\0';
                            meta.colorARGB = trackColor;
                            meta.hasComment = false;
                            metaMgr->setRegionMetadata(regId, meta, true);
                        }
                    }
                    trackManager->compileTimelineSnapshot();
                }
                sessionManager_->triggerSessionRefresh();
            }
        }
    }
}

uint32_t RecordingController::getActiveRecordings(ActiveRecordingInfo* out, uint32_t maxCount) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    bool isRecording = false;
    uint64_t currentSample = 0;

    if (audioEngine_) {
        isRecording = (audioEngine_->getTransportState() == TransportState::RECORDING);
        currentSample = audioEngine_->getTransportPosition();
    }

    uint32_t count = 0;
    for (auto& [tid, rec] : activeRecordings_) {
        if (count >= maxCount) break;
        out[count].trackId = TrackID{tid, 1};
        out[count].startSample = rec.startSample;
        out[count].currentSample = isRecording ? currentSample : rec.startSample;
        
        if (rec.peakQueue) {
            float peak = 0.0f;
            while (rec.peakQueue->pop(peak)) {
                rec.livePeaks.push_back(peak);
            }
        }
        out[count].livePeaks = rec.livePeaks.data();
        out[count].numPeaks = static_cast<uint32_t>(rec.livePeaks.size());
        
        count++;
    }
    return count;
}

} // namespace bridge
