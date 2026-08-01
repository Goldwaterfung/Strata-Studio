// src/Middle Bridge/timeline_controller.cpp
#include "timeline/timeline_controller.h"
#include "project/isession_manager.h"
#include "automation/iautomation_controller.h"
#include "recording/irecording_controller.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "musical_composition/playlist/iplaylist.h"
#include "musical_composition/region_manager/iaudio_region_source_manager.h"
#include "Hardware/OS abstraction/audio/iaudio_driver.h"
#include "musical_composition/command_history/timeline_commands.h"
#include "musical_composition/command_history/command_history_impl.h"
#include "musical_composition/interfaces/imarker_manager.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <iostream>

namespace bridge {

TimelineController::TimelineController(
    Layer3::ITransport* transport,
    Layer2::ITempoService* tempoService
)
    : transport_(transport)
    , tempoService_(tempoService)
{
}

void TimelineController::togglePlay() {
    if (!transport_) return;
    if (isPlaying()) {
        stop();
    } else {
        play();
    }
}

void TimelineController::play() {
    if (!transport_) return;

    std::cout << "\n[TIMELINE PLAYBACK DIAGNOSTIC] play() triggered. Playhead Frame: " << transport_->getPosition() << std::endl;
    
    if (audioDriver_) {
        auto config = audioDriver_->getStreamConfig();
        auto inDevInfo = audioDriver_->getDeviceInfo(config.inputDeviceIndex);
        auto outDevInfo = audioDriver_->getDeviceInfo(config.outputDeviceIndex);
        std::cout << "  -> Active Audio Driver Stream Configuration:" << std::endl;
        std::cout << "     * Output Device: " << outDevInfo.name << " (Index=" << config.outputDeviceIndex << ")" << std::endl;
        std::cout << "     * Input Device:  " << inDevInfo.name << " (Index=" << config.inputDeviceIndex << ")" << std::endl;
        std::cout << "     * Sample Rate:   " << config.sampleRate << " Hz" << std::endl;
        std::cout << "     * Buffer Size:   " << config.bufferSize << " frames" << std::endl;
        std::cout << "     * Channels:      Output=" << config.numOutputChannels << ", Input=" << config.numInputChannels << std::endl;
    } else {
        std::cout << "  -> Warning: audioDriver_ not wired to TimelineController." << std::endl;
    }

    if (sessionManager_ && stringRegistry_) {
        if (auto* session = sessionManager_->getActiveSession()) {
            if (auto* trackManager = session->getTrackManager()) {
                std::vector<composition::TrackID> tracks = trackManager->getAllTrackIDs();
                std::cout << "  -> Total Tracks in active project session: " << tracks.size() << std::endl;
                for (auto trackId : tracks) {
                    composition::TrackCreateInfo info{};
                    if (trackManager->getTrackInfo(trackId, info)) {
                        std::string trackName = "Unknown";
                        stringRegistry_->getString(info.nameId, trackName);
                        std::cout << "     * Track ID=" << trackId.id << " [Name: " << trackName 
                                  << ", Type: " << static_cast<int>(info.type) << "]" << std::endl;
                        
                        if (auto* playlist = trackManager->getPlaylist(trackId)) {
                            composition::IPlaylist::RegionInfo scratch[64];
                            uint32_t count = playlist->getAllRegions(scratch, 64);
                            std::cout << "       - Active Audio Regions count: " << count << std::endl;
                            for (uint32_t i = 0; i < count; ++i) {
                                std::string filePath = "Unknown";
                                if (auto* srcManager = session->getRegionSourceManager()) {
                                    composition::AudioSourceDescriptor srcDesc{};
                                    if (srcManager->getSource(scratch[i].region.sourceId, srcDesc)) {
                                        stringRegistry_->getString(srcDesc.nameId, filePath);
                                    }
                                }
                                std::cout << "         [Region ID: " << scratch[i].id.id << "] File Path: " << filePath 
                                          << " | Pos: " << scratch[i].region.positionSample 
                                          << " | SrcStart: " << scratch[i].region.sourceStartSample 
                                          << " | SrcLen: " << scratch[i].region.sourceLength << std::endl;
                            }
                        }
                    }
                }
            }
        }
    } else {
        std::cout << "  -> Warning: sessionManager_ or stringRegistry_ not wired to TimelineController." << std::endl;
    }

    transport_->play();
    if (automationController_) {
        automationController_->onTransportStateChanged(true);
        if (transport_->getState() == TransportState::RECORDING) {
            automationController_->onTransportRecordingStarted();
            if (recordingController_) {
                recordingController_->onTransportRecordingStarted(transport_->getPosition());
            }
        }
    }
}

void TimelineController::stop() {
    if (!transport_) return;
    std::cout << "[TIMELINE PLAYBACK DIAGNOSTIC] stop() triggered. Playhead stopped at Frame: " << transport_->getPosition() << "\n" << std::endl;
    bool wasRecording = (transport_->getState() == TransportState::RECORDING);
    transport_->stop();
    if (automationController_) {
        automationController_->onTransportStateChanged(false);
        if (wasRecording) {
            automationController_->onTransportRecordingStopped();
            if (recordingController_) {
                recordingController_->onTransportRecordingStopped(transport_->getPosition());
            }
        } else {
            automationController_->stopActiveRecording();
        }
    }
}

void TimelineController::setRecordArmed(bool armed) {
    if (!transport_) return;
    transport_->setRecordArmed(armed);
    if (armed) {
        if (automationController_ && transport_->getState() == TransportState::RECORDING) {
            automationController_->onTransportRecordingStarted();
        }
    } else {
        if (transport_->getState() == TransportState::RECORDING) {
            transport_->setState(TransportState::PLAYING);
            if (automationController_) {
                automationController_->onTransportRecordingStopped();
            }
        }
    }
}

void TimelineController::seekToFrame(uint64_t framePosition) {
    if (!transport_) return;
    transport_->seek(framePosition, Layer3::ITransport::SeekMode::IMMEDIATE);
}

void TimelineController::seekToTimeSeconds(double seconds) {
    if (!transport_ || !tempoService_) return;
    double sampleRate = tempoService_->getSampleRate();
    if (sampleRate <= 0.0) return;
    uint64_t frame = static_cast<uint64_t>(std::max(0.0, seconds * sampleRate));
    transport_->seek(frame, Layer3::ITransport::SeekMode::IMMEDIATE);
}

void TimelineController::seekToMusicalGrid(double bar, double beat) {
    if (!transport_ || !tempoService_) return;
    // Layer2::BBTPosition is 1-based for bar and beat, 0-based for tick
    uint32_t b = static_cast<uint32_t>(std::max(1.0, bar));
    uint32_t be = static_cast<uint32_t>(std::max(1.0, beat));
    Layer2::BBTPosition bbt(b, be, 0);
    uint64_t frame = tempoService_->bbtToSamples(bbt);
    transport_->seek(frame, Layer3::ITransport::SeekMode::IMMEDIATE);
}

void TimelineController::setBPM(double bpm) {
    if (!transport_ || !tempoService_) return;
    double clampedBpm = std::clamp(bpm, 1.0, 1000.0);

    if (!isApplyingDelta_) {
        auto* session = sessionManager_ ? sessionManager_->getActiveSession() : nullptr;
        auto* history = session ? session->getCommandHistory() : nullptr;
        if (history) {
            composition::ProjectDelta delta{};
            delta.subsystemId = composition::SubsystemID::TEMPO_TIMELINE;
            delta.operationType = composition::TempoTimelineOps::SET_BPM;
            delta.targetId = 0;

            composition::SetBPMPayload payload{};
            payload.oldBpm = tempoService_->getTempoAtPosition(0);
            payload.newBpm = clampedBpm;

            delta.oldStateSize = sizeof(composition::SetBPMPayload);
            std::memcpy(delta.oldState, &payload, sizeof(composition::SetBPMPayload));
            delta.newStateSize = sizeof(composition::SetBPMPayload);
            std::memcpy(delta.newState, &payload, sizeof(composition::SetBPMPayload));

            history->pushDelta(delta);
        }
    }

    tempoService_->setTempoAtPosition(clampedBpm, 0);
    transport_->updateTempoCache();
    if (sessionManager_) sessionManager_->onTempoMapChanged(tempoService_);
}

bool TimelineController::isTempoAutomated() const {
    if (!tempoService_) return false;
    Layer2::ITempoService::TempoPoint points[2];
    uint32_t count = tempoService_->getTempoRange(0, UINT64_MAX, points, 2);
    if (count > 1) {
        return true;
    }
    if (count == 1 && points[0].positionSample > 0) {
        return true;
    }
    return false;
}

void TimelineController::removeTempoPoint(uint64_t framePosition) {
    if (!tempoService_ || !transport_) return;

    if (!isApplyingDelta_) {
        auto* session = sessionManager_ ? sessionManager_->getActiveSession() : nullptr;
        auto* history = session ? session->getCommandHistory() : nullptr;
        if (history) {
            Layer2::ITempoService::TempoPoint existing{};
            uint32_t count = tempoService_->getTempoRange(framePosition, framePosition, &existing, 1);
            if (count > 0 && existing.positionSample == framePosition) {
                composition::ProjectDelta delta{};
                delta.subsystemId = composition::SubsystemID::TEMPO_TIMELINE;
                delta.operationType = composition::TempoTimelineOps::REMOVE_TEMPO_POINT;
                delta.targetId = 0;

                composition::TempoPointPayload oldPayload{};
                oldPayload.framePosition = framePosition;
                oldPayload.bpm = existing.bpm;

                delta.oldStateSize = sizeof(composition::TempoPointPayload);
                std::memcpy(delta.oldState, &oldPayload, sizeof(composition::TempoPointPayload));
                delta.newStateSize = 0;

                history->pushDelta(delta);
            }
        }
    }

    tempoService_->removeTempoEventAtPosition(framePosition);
    transport_->updateTempoCache();
    if (sessionManager_) sessionManager_->onTempoMapChanged(tempoService_);
}

void TimelineController::addTempoPoint(uint64_t framePosition, double bpm) {
    if (!tempoService_ || !transport_) return;
    double clampedBpm = std::clamp(bpm, 1.0, 1000.0);

    if (!isApplyingDelta_) {
        auto* session = sessionManager_ ? sessionManager_->getActiveSession() : nullptr;
        auto* history = session ? session->getCommandHistory() : nullptr;
        if (history) {
            composition::ProjectDelta delta{};
            delta.subsystemId = composition::SubsystemID::TEMPO_TIMELINE;
            delta.operationType = composition::TempoTimelineOps::ADD_TEMPO_POINT;
            delta.targetId = 0;

            composition::TempoPointPayload payload{};
            payload.framePosition = framePosition;
            payload.bpm = clampedBpm;

            delta.newStateSize = sizeof(composition::TempoPointPayload);
            std::memcpy(delta.newState, &payload, sizeof(composition::TempoPointPayload));

            // Check if point already exists to populate oldState
            Layer2::ITempoService::TempoPoint existing{};
            uint32_t count = tempoService_->getTempoRange(framePosition, framePosition, &existing, 1);
            if (count > 0 && existing.positionSample == framePosition) {
                composition::TempoPointPayload oldPayload{};
                oldPayload.framePosition = framePosition;
                oldPayload.bpm = existing.bpm;
                delta.oldStateSize = sizeof(composition::TempoPointPayload);
                std::memcpy(delta.oldState, &oldPayload, sizeof(composition::TempoPointPayload));
            } else {
                delta.oldStateSize = 0;
            }

            history->pushDelta(delta);
        }
    }

    tempoService_->setTempoAtPosition(clampedBpm, framePosition);
    transport_->updateTempoCache();
    if (sessionManager_) sessionManager_->onTempoMapChanged(tempoService_);
}

uint32_t TimelineController::getTempoPoints(uint64_t startFrame, uint64_t endFrame,
                                            VisualTempoPoint* outPoints, uint32_t maxCount) const {
    if (!tempoService_ || !outPoints || maxCount == 0) return 0;
    
    std::vector<Layer2::ITempoService::TempoPoint> buffer(maxCount);
    uint32_t count = tempoService_->getTempoRange(startFrame, endFrame, buffer.data(), maxCount);
    
    for (uint32_t i = 0; i < count; ++i) {
        outPoints[i].framePosition = buffer[i].positionSample;
        outPoints[i].bpm = buffer[i].bpm;
    }
    return count;
}

void TimelineController::setTimeSignature(VisualTimeSignature timeSig) {
    if (!transport_ || !tempoService_) return;
    // Sanity-check denominator (must be power of 2)
    uint8_t den = timeSig.denominator;
    if (den != 2 && den != 4 && den != 8 && den != 16) {
        den = 4;
    }
    uint8_t num = timeSig.numerator > 0 ? timeSig.numerator : 4;

    uint64_t pos = transport_->getPosition();

    if (!isApplyingDelta_) {
        auto* session = sessionManager_ ? sessionManager_->getActiveSession() : nullptr;
        auto* history = session ? session->getCommandHistory() : nullptr;
        if (history) {
            composition::ProjectDelta delta{};
            delta.subsystemId = composition::SubsystemID::TEMPO_TIMELINE;
            delta.operationType = composition::TempoTimelineOps::SET_TIME_SIGNATURE;
            delta.targetId = 0;

            uint8_t oldNum = 4, oldDen = 4;
            tempoService_->getMeterAtPosition(pos, oldNum, oldDen);

            composition::TimeSignaturePayload payload{};
            payload.framePosition = pos;
            payload.oldNumerator = oldNum;
            payload.oldDenominator = oldDen;
            payload.newNumerator = num;
            payload.newDenominator = den;

            delta.oldStateSize = sizeof(composition::TimeSignaturePayload);
            std::memcpy(delta.oldState, &payload, sizeof(composition::TimeSignaturePayload));
            delta.newStateSize = sizeof(composition::TimeSignaturePayload);
            std::memcpy(delta.newState, &payload, sizeof(composition::TimeSignaturePayload));

            history->pushDelta(delta);
        }
    }

    tempoService_->setMeterAtPosition(num, den, pos);
    transport_->updateTempoCache();
}

void TimelineController::setLoopRange(uint64_t startFrame, uint64_t endFrame) {
    if (!transport_) return;

    if (!isApplyingDelta_) {
        auto* session = sessionManager_ ? sessionManager_->getActiveSession() : nullptr;
        auto* history = session ? session->getCommandHistory() : nullptr;
        if (history) {
            composition::ProjectDelta delta{};
            delta.subsystemId = composition::SubsystemID::TEMPO_TIMELINE;
            delta.operationType = composition::TempoTimelineOps::SET_LOOP_RANGE;
            delta.targetId = 0;

            auto loopState = transport_->getLoopState();

            composition::LoopRangePayload payload{};
            payload.oldStartFrame = loopState.startSample;
            payload.oldEndFrame = loopState.endSample;
            payload.newStartFrame = startFrame;
            payload.newEndFrame = endFrame;

            delta.oldStateSize = sizeof(composition::LoopRangePayload);
            std::memcpy(delta.oldState, &payload, sizeof(composition::LoopRangePayload));
            delta.newStateSize = sizeof(composition::LoopRangePayload);
            std::memcpy(delta.newState, &payload, sizeof(composition::LoopRangePayload));

            history->pushDelta(delta);
        }
    }

    transport_->setLoopRange(startFrame, endFrame);
}

void TimelineController::setLoopEnabled(bool enabled) {
    if (!transport_) return;

    if (!isApplyingDelta_) {
        auto* session = sessionManager_ ? sessionManager_->getActiveSession() : nullptr;
        auto* history = session ? session->getCommandHistory() : nullptr;
        if (history) {
            composition::ProjectDelta delta{};
            delta.subsystemId = composition::SubsystemID::TEMPO_TIMELINE;
            delta.operationType = composition::TempoTimelineOps::SET_LOOP_ENABLED;
            delta.targetId = 0;

            composition::LoopEnabledPayload payload{};
            payload.oldEnabled = transport_->getLoopState().isLooping();
            payload.newEnabled = enabled;

            delta.oldStateSize = sizeof(composition::LoopEnabledPayload);
            std::memcpy(delta.oldState, &payload, sizeof(composition::LoopEnabledPayload));
            delta.newStateSize = sizeof(composition::LoopEnabledPayload);
            std::memcpy(delta.newState, &payload, sizeof(composition::LoopEnabledPayload));

            history->pushDelta(delta);
        }
    }

    transport_->setLoopEnabled(enabled);
}

uint64_t TimelineController::getCurrentFrame() const {
    if (!transport_) return 0;
    return transport_->getPosition();
}

double TimelineController::getCurrentSeconds() const {
    if (!transport_ || !tempoService_) return 0.0;
    double sampleRate = tempoService_->getSampleRate();
    if (sampleRate <= 0.0) return 0.0;
    return static_cast<double>(transport_->getPosition()) / sampleRate;
}

bool TimelineController::isPlaying() const {
    if (!transport_) return false;
    auto state = transport_->getState();
    return state == TransportState::PLAYING || state == TransportState::RECORDING;
}

bool TimelineController::isRecording() const {
    if (!transport_) return false;
    return transport_->getState() == TransportState::RECORDING;
}

bool TimelineController::isRecordArmed() const {
    if (!transport_) return false;
    return transport_->isRecordArmed();
}

bool TimelineController::isLooping() const {
    if (!transport_) return false;
    return transport_->getLoopState().isLooping();
}

double TimelineController::getBPM() const {
    if (!transport_ || !tempoService_) return 120.0;
    return tempoService_->getTempoAtPosition(transport_->getPosition());
}

double TimelineController::pixelsToFrames(float pixels, float zoomFactor) const {
    if (zoomFactor <= 0.0f) return 0.0;
    return static_cast<double>(pixels) / static_cast<double>(zoomFactor);
}

float TimelineController::framesToPixels(uint64_t frames, float zoomFactor) const {
    if (zoomFactor <= 0.0f) return 0.0f;
    return static_cast<float>(frames) * zoomFactor;
}

double TimelineController::getSampleRate() const {
    if (!tempoService_) return 48000.0;
    return tempoService_->getSampleRate();
}

uint64_t TimelineController::getLoopStart() const {
    if (!transport_) return 0;
    return transport_->getLoopState().startSample;
}

uint64_t TimelineController::getLoopEnd() const {
    if (!transport_) return 0;
    return transport_->getLoopState().endSample;
}

void TimelineController::setPlaybackMode(PlaybackMode mode) {
    playbackMode_ = mode;
}

PlaybackMode TimelineController::getPlaybackMode() const {
    return playbackMode_;
}

void TimelineController::getCurrentBBT(uint32_t& bar, uint32_t& beat, uint32_t& tick) const {
    if (!transport_ || !tempoService_) {
        bar = 1;
        beat = 1;
        tick = 0;
        return;
    }
    Layer2::BBTPosition bbt = tempoService_->samplesToBBT(transport_->getPosition());
    bar = bbt.bar;
    beat = bbt.beat;
    tick = bbt.tick;
}

uint64_t TimelineController::samplesToTicks(uint64_t samples) const {
    if (!tempoService_) return 0;
    double beats = tempoService_->samplesToBeats(samples);
    return static_cast<uint64_t>(std::round(beats * tempoService_->getTicksPerBeat()));
}

uint64_t TimelineController::ticksToSamples(uint64_t ticks) const {
    if (!tempoService_) return 0;
    uint32_t tpb = tempoService_->getTicksPerBeat();
    if (tpb == 0) return 0;
    double beats = static_cast<double>(ticks) / tpb;
    return tempoService_->beatsToSamples(beats);
}

uint32_t TimelineController::getTicksPerBeat() const {
    if (!tempoService_) return 960;
    return tempoService_->getTicksPerBeat();
}

void TimelineController::frameToBBT(uint64_t frame, uint32_t& bar, uint32_t& beat, uint32_t& tick) const {
    if (!tempoService_) {
        bar = 1;
        beat = 1;
        tick = 0;
        return;
    }
    Layer2::BBTPosition bbt = tempoService_->samplesToBBT(frame);
    bar = bbt.bar;
    beat = bbt.beat;
    tick = bbt.tick;
}

uint64_t TimelineController::bbtToFrame(uint32_t bar, uint32_t beat, uint32_t tick) const {
    if (!tempoService_) return 0;
    Layer2::BBTPosition bbt(bar, beat, tick);
    return tempoService_->bbtToSamples(bbt);
}

void TimelineController::getTimeSignatureAtFrame(uint64_t frame, uint8_t& numerator, uint8_t& denominator) const {
    if (!tempoService_) {
        numerator = 4;
        denominator = 4;
        return;
    }
    tempoService_->getMeterAtPosition(frame, numerator, denominator);
}

void TimelineController::setMetronomeEnabled(bool enabled) {
    if (transport_) {
        transport_->setMetronomeEnabled(enabled);
    }
    metronomeEnabled_ = enabled;
}

bool TimelineController::isMetronomeEnabled() const {
    return transport_ ? transport_->isMetronomeEnabled() : metronomeEnabled_;
}

void TimelineController::setCountInEnabled(bool enabled) {
    countInEnabled_ = enabled;
}

bool TimelineController::isCountInEnabled() const {
    return countInEnabled_;
}

void TimelineController::setCountInBars(uint8_t bars) {
    countInBars_ = (bars > 0) ? bars : 1;
}

uint8_t TimelineController::getCountInBars() const {
    return countInBars_;
}

// ---------------------------------------------------------------------------
// Named Markers
// ---------------------------------------------------------------------------

void TimelineController::addMarker(uint64_t framePosition, const char* label, uint32_t colorARGB) {
    auto* session = sessionManager_ ? sessionManager_->getActiveSession() : nullptr;
    if (session) {
        session->getMarkerManager()->addMarker(MarkerUUID{}, framePosition, label, colorARGB, true);
    }
}

void TimelineController::removeMarker(const MarkerUUID& uuid) {
    auto* session = sessionManager_ ? sessionManager_->getActiveSession() : nullptr;
    if (session) {
        session->getMarkerManager()->removeMarker(uuid, true);
    }
}

void TimelineController::updateMarker(const MarkerUUID& uuid, uint64_t framePosition, const char* label, uint32_t colorARGB) {
    auto* session = sessionManager_ ? sessionManager_->getActiveSession() : nullptr;
    if (session) {
        session->getMarkerManager()->updateMarker(uuid, framePosition, label, colorARGB, true);
    }
}

uint32_t TimelineController::getMarkersInRange(uint64_t startFrame, uint64_t endFrame,
                                                VisualMarker* outMarkers, uint32_t maxCount) const {
    auto* session = sessionManager_ ? sessionManager_->getActiveSession() : nullptr;
    if (session) {
        std::vector<composition::MarkerInfo> tempMarkers(maxCount);
        uint32_t count = session->getMarkerManager()->getMarkersInRange(startFrame, endFrame, tempMarkers.data(), maxCount);
        double sampleRate = static_cast<double>(session->getMetadata().sampleRate);
        if (sampleRate <= 0.0) sampleRate = 44100.0;
        
        for (uint32_t i = 0; i < count; ++i) {
            outMarkers[i].uuid = tempMarkers[i].uuid;
            outMarkers[i].framePosition = tempMarkers[i].framePosition;
            std::strncpy(outMarkers[i].label, tempMarkers[i].label, MAX_NAME_LENGTH - 1);
            outMarkers[i].label[MAX_NAME_LENGTH - 1] = '\0';
            outMarkers[i].colorARGB = tempMarkers[i].colorARGB;
            outMarkers[i].markerNumber = tempMarkers[i].markerNumber;
            
            double totalSeconds = static_cast<double>(tempMarkers[i].framePosition) / sampleRate;
            outMarkers[i].locationSeconds = totalSeconds;
            
            uint32_t hours = static_cast<uint32_t>(totalSeconds / 3600.0);
            double remainder = totalSeconds - (hours * 3600.0);
            uint32_t minutes = static_cast<uint32_t>(remainder / 60.0);
            remainder = remainder - (minutes * 60.0);
            uint32_t seconds = static_cast<uint32_t>(remainder);
            double subSeconds = remainder - seconds;
            uint32_t frames = static_cast<uint32_t>(std::round(subSeconds * 30.0));
            if (frames >= 30) {
                frames = 0;
                seconds += 1;
                if (seconds >= 60) {
                    seconds = 0;
                    minutes += 1;
                    if (minutes >= 60) {
                        minutes = 0;
                        hours += 1;
                    }
                }
            }
            std::snprintf(outMarkers[i].timecode, sizeof(outMarkers[i].timecode), "%02u:%02u:%02u:%02u", hours, minutes, seconds, frames);
        }
        return count;
    }
    return 0;
}

void TimelineController::onSessionChanging() {
}

void TimelineController::onSessionChanged(composition::IProjectSession* newSession) {
    if (newSession) {
        auto* history = newSession->getCommandHistory();
        if (history) {
            auto* historyImpl = static_cast<composition::CommandHistoryImpl*>(history);
            historyImpl->registerHandler(composition::SubsystemID::TEMPO_TIMELINE, [this](const composition::ProjectDelta& d, bool u) {
                this->applyTempoTimelineDelta(d, u);
            });
        }
    }
}

void TimelineController::applyTempoTimelineDelta(const composition::ProjectDelta& delta, bool isUndo) {
    if (!transport_ || !tempoService_) return;
    
    isApplyingDelta_ = true;
    
    switch (delta.operationType) {
        case composition::TempoTimelineOps::SET_BPM: {
            composition::SetBPMPayload payload{};
            std::memcpy(&payload, delta.newState, sizeof(composition::SetBPMPayload));
            double targetBpm = isUndo ? payload.oldBpm : payload.newBpm;
            tempoService_->setTempoAtPosition(targetBpm, 0);
            transport_->updateTempoCache();
            if (sessionManager_) sessionManager_->onTempoMapChanged(tempoService_);
            break;
        }
        case composition::TempoTimelineOps::ADD_TEMPO_POINT: {
            if (isUndo) {
                if (delta.oldStateSize > 0) {
                    composition::TempoPointPayload oldPayload{};
                    std::memcpy(&oldPayload, delta.oldState, sizeof(composition::TempoPointPayload));
                    tempoService_->setTempoAtPosition(oldPayload.bpm, oldPayload.framePosition);
                } else {
                    composition::TempoPointPayload newPayload{};
                    std::memcpy(&newPayload, delta.newState, sizeof(composition::TempoPointPayload));
                    tempoService_->removeTempoEventAtPosition(newPayload.framePosition);
                }
            } else {
                composition::TempoPointPayload newPayload{};
                std::memcpy(&newPayload, delta.newState, sizeof(composition::TempoPointPayload));
                tempoService_->setTempoAtPosition(newPayload.bpm, newPayload.framePosition);
            }
            transport_->updateTempoCache();
            if (sessionManager_) sessionManager_->onTempoMapChanged(tempoService_);
            break;
        }
        case composition::TempoTimelineOps::REMOVE_TEMPO_POINT: {
            composition::TempoPointPayload oldPayload{};
            std::memcpy(&oldPayload, delta.oldState, sizeof(composition::TempoPointPayload));
            if (isUndo) {
                tempoService_->setTempoAtPosition(oldPayload.bpm, oldPayload.framePosition);
            } else {
                tempoService_->removeTempoEventAtPosition(oldPayload.framePosition);
            }
            transport_->updateTempoCache();
            if (sessionManager_) sessionManager_->onTempoMapChanged(tempoService_);
            break;
        }
        case composition::TempoTimelineOps::SET_TIME_SIGNATURE: {
            composition::TimeSignaturePayload payload{};
            std::memcpy(&payload, delta.newState, sizeof(composition::TimeSignaturePayload));
            uint8_t num = isUndo ? payload.oldNumerator : payload.newNumerator;
            uint8_t den = isUndo ? payload.oldDenominator : payload.newDenominator;
            tempoService_->setMeterAtPosition(num, den, payload.framePosition);
            transport_->updateTempoCache();
            break;
        }
        case composition::TempoTimelineOps::SET_LOOP_RANGE: {
            composition::LoopRangePayload payload{};
            std::memcpy(&payload, delta.newState, sizeof(composition::LoopRangePayload));
            uint64_t start = isUndo ? payload.oldStartFrame : payload.newStartFrame;
            uint64_t end = isUndo ? payload.oldEndFrame : payload.newEndFrame;
            transport_->setLoopRange(start, end);
            break;
        }
        case composition::TempoTimelineOps::SET_LOOP_ENABLED: {
            composition::LoopEnabledPayload payload{};
            std::memcpy(&payload, delta.newState, sizeof(composition::LoopEnabledPayload));
            bool enabled = isUndo ? payload.oldEnabled : payload.newEnabled;
            transport_->setLoopEnabled(enabled);
            break;
        }
        default:
            break;
    }
    
    isApplyingDelta_ = false;
}

} // namespace bridge
