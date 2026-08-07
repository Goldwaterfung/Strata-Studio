// src/Middle Bridge/arrangement_controller.cpp
#include "Middle Bridge/timeline/arrangement_controller.h"
#include "musical_composition/interfaces/iregion_metadata_manager.h"
#include "musical_composition/playlist/iplaylist.h"
#include "musical_composition/region_manager/region_source_manager_impl.h"
#include "musical_composition/automation/iautomation_lane.h"
#include "musical_composition/automation/iautomation_lane_manager.h"
#include "musical_composition/automation/automation_lane_manager_impl.h"
#include "musical_composition/automation/automation_lane_impl.h"
#include "musical_composition/command_history/icommand_history.h"
#include "musical_composition/midi_sequencer/imidi_sequencer.h"
#include "Media management/codecs/icodec_factory.h"
#include "Media management/waveforms/iwaveform_renderer.h"
#include "recording/irecording_controller.h"
#include "Hardware/OS abstraction/filesystem/ifile_system.h"
#include "DSP nodes/sequencer/audio_sequencer_node.h"
#include "Core audio engine/streaming/ibutler_thread.h"
#include <cstring>
#include <algorithm>
#include <iostream>

namespace bridge {

using composition::RegionID;
using composition::SourceID;

ArrangementController::ArrangementController(
    ISessionManager* sessionManager,
    Layer2::IStringRegistry* stringRegistry,
    MediaManagement::IMediaRegistry* mediaRegistry,
    MediaManagement::IWaveformRenderer* waveformRenderer,
    Layer1::IFileSystem* fs
) : sessionManager_(sessionManager),
    stringRegistry_(stringRegistry),
    mediaRegistry_(mediaRegistry),
    waveformRenderer_(waveformRenderer),
    fs_(fs) {
    if (sessionManager_) {
        sessionManager_->registerChangeListener(this);
    }
}

ArrangementController::~ArrangementController() {
    if (sessionManager_) {
        sessionManager_->unregisterChangeListener(this);
    }
}

composition::ITrackManager* ArrangementController::getTrackManager() const {
    if (sessionManager_) {
        if (auto* session = sessionManager_->getActiveSession()) {
            return session->getTrackManager();
        }
    }
    return nullptr;
}

composition::IAudioRegionSourceManager* ArrangementController::getSourceManager() const {
    if (sessionManager_) {
        if (auto* session = sessionManager_->getActiveSession()) {
            return session->getRegionSourceManager();
        }
    }
    return nullptr;
}

RegionID ArrangementController::importAudioClip(TrackID targetTrack, const char* filePath, uint64_t startFrame) {
    auto* trackManager = getTrackManager();
    auto* sourceManager = getSourceManager();
    if (!trackManager || !sourceManager || !stringRegistry_ || !filePath) {
        return {0, 0};
    }

    // 1. Get playlist
    composition::IPlaylist* playlist = trackManager->getPlaylist(targetTrack);
    if (!playlist) {
        return {0, 0};
    }

    // 2. Register filepath in StringRegistry
    uint32_t nameId = stringRegistry_->registerString(filePath);

    // 3. Register audio source in IAudioRegionSourceManager
    composition::AudioSourceDescriptor desc{};
    desc.sourceId = SourceID::invalid();
    desc.nameId = nameId;
    desc.mediaId = 0;

    // Extract actual file characteristics via Layer 6 codecs
    uint64_t realMediaId = 0;
    auto codecFactory = MediaManagement::ICodecFactory::create();
    if (codecFactory) {
        auto reader = codecFactory->createReader(filePath);
        if (reader && reader->isValid()) {
            desc.totalLengthSamples = reader->getTotalFrames();
            desc.channelCount = reader->getNumChannels();
            desc.sampleRate = reader->getSampleRate();
        }
    }
    if (desc.totalLengthSamples == 0) desc.totalLengthSamples = 441000;
    if (desc.channelCount == 0) desc.channelCount = 2;
    if (desc.sampleRate == 0) desc.sampleRate = 44100;

    // Register in IMediaRegistry to get a real MediaID for waveform lookup
    if (mediaRegistry_) {
        MediaManagement::AssetInfo assetInfo{};
        assetInfo.pathId = nameId;
        assetInfo.nameId = nameId;
        assetInfo.durationSamples = desc.totalLengthSamples;
        assetInfo.sampleRate = desc.sampleRate;
        assetInfo.numChannels = static_cast<uint16_t>(desc.channelCount);
        assetInfo.sizeBytes = 0; // Not critical for waveform
        assetInfo.bitDepth = 16;

        MediaID mediaId = mediaRegistry_->registerAsset(assetInfo);
        realMediaId = mediaId.toRaw();
        desc.mediaId = realMediaId;

        // Trigger async waveform peak generation in background
        if (waveformRenderer_) {
            waveformRenderer_->getWaveform(mediaId, MediaManagement::WaveformResolution::OVERVIEW);
            waveformRenderer_->getWaveform(mediaId, MediaManagement::WaveformResolution::HIGH);
        }
    }

    SourceID srcId = sourceManager->registerSource(desc, filePath);

    if (!sessionManager_ || !sessionManager_->getActiveSession()) return {0, 0};
    uint32_t projSampleRate = sessionManager_->getActiveSession()->getMetadata().sampleRate;
    if (projSampleRate == 0) projSampleRate = 44100;

    composition::TrackCreateInfo trackInfo{};
    trackManager->getTrackInfo(targetTrack, trackInfo);

    float tempoBpm = sessionManager_->getActiveSession()->getMetadata().initialTempoBPM;
    if (tempoBpm <= 0.0f) tempoBpm = 120.0f;

    // 4. Build region primitive
    composition::TimelineRegion region;
    region.type = composition::RegionType::AUDIO;
    region.sourceId = srcId;
    region.positionSample = startFrame;
    region.sourceStartSample = 0;

    double scaleFactor = static_cast<double>(projSampleRate) / static_cast<double>(desc.sampleRate);
    region.sourceLength = static_cast<uint64_t>(std::round(static_cast<double>(desc.totalLengthSamples) * scaleFactor));

    region.fadeInSamples = 0;
    region.fadeOutSamples = 0;
    region.gain = 1.0f;
    region.isMuted = false;
    region.warpMode = WarpMode::BYPASS;
    region.playbackRatio = 1.0f;
    region.sourceBpm = tempoBpm;

    // 5. Add to playlist
    RegionID regId = playlist->addRegion(region, composition::IPlaylist::AUTO_LAYER);
    if (regId.isValid()) {
        trackManager->compileTimelineSnapshot();
        
        std::string path = filePath;
        size_t lastSlash = path.find_last_of("/\\");
        std::string filename = (lastSlash == std::string::npos) ? path : path.substr(lastSlash + 1);
        initializeRegionMetadata(regId, filename.c_str(), trackInfo.colorARGB);

        auto pipeDesc = trackManager->getPipelineDescriptor(targetTrack);
        std::cout << "[ARRANGEMENT CONTROLLER DIAGNOSTIC] Region added successfully. targetTrack ID=" << targetTrack.id 
                  << ", sourceNodeID=" << pipeDesc.sourceNode.id << std::endl;
        if (DSP::AudioSequencerFactory::s_butlerThread) {
            DSP::AudioSequencerFactory::s_butlerThread->registerSourcePath(srcId.id, filePath);
            std::cout << "  -> Source path registered to butler thread for sourceId: " << srcId.id << " -> " << filePath << std::endl;
        }
    }
    return regId;
}

RegionID ArrangementController::insertMidiClip(TrackID targetTrack, uint64_t startFrame, uint64_t durationFrames) {
    auto* trackManager = getTrackManager();
    if (!trackManager) return {0, 0};

    composition::IPlaylist* playlist = trackManager->getPlaylist(targetTrack);
    if (!playlist) return {0, 0};

    if (!sessionManager_ || !sessionManager_->getActiveSession()) return {0, 0};
    float tempoBpm = sessionManager_->getActiveSession()->getMetadata().initialTempoBPM;
    if (tempoBpm <= 0.0f) return {0, 0};

    composition::TrackCreateInfo trackInfo{};
    if (!trackManager->getTrackInfo(targetTrack, trackInfo)) return {0, 0};

    composition::ClipID clipId{ ++composition::getGlobalClipIdCounter(), 1 };

    composition::TimelineRegion region;
    region.type = composition::RegionType::MIDI;
    region.sourceId = SourceID::fromRaw(clipId.toRaw());
    region.positionSample = startFrame;
    region.sourceStartSample = 0;
    region.sourceLength = durationFrames;
    region.fadeInSamples = 0;
    region.fadeOutSamples = 0;
    region.gain = 1.0f;
    region.isMuted = false;
    region.warpMode = WarpMode::BYPASS;
    region.playbackRatio = 1.0f;
    region.sourceBpm = tempoBpm;

    RegionID regId = playlist->addRegion(region, composition::IPlaylist::AUTO_LAYER);
    if (regId.isValid()) {
        trackManager->compileTimelineSnapshot();
        
        initializeRegionMetadata(regId, trackInfo.nameId > 0 ? "MIDI" : "Clip", trackInfo.colorARGB);

        if (auto* seq = trackManager->getMIDISequencer(targetTrack)) {
            seq->updateClipPosition(clipId, startFrame, durationFrames);
        }
    }
    return regId;
}

RegionID ArrangementController::insertAutomationClip(
    [[maybe_unused]] TrackID targetTrack,
    [[maybe_unused]] NodeID dspNode,
    [[maybe_unused]] uint32_t parameterIndex,
    [[maybe_unused]] uint64_t startFrame,
    [[maybe_unused]] uint64_t durationFrames
) {
    return composition::RegionID::invalid();
}

composition::IPlaylist* ArrangementController::findPlaylistForRegion(RegionID regionId, composition::IPlaylist::RegionInfo& outInfo, TrackID& outTrackId) const {
    auto* trackManager = getTrackManager();
    if (!trackManager) return nullptr;

    std::vector<TrackID> tracks = trackManager->getAllTrackIDs();
    
    uint32_t capacity = 512;
    if (regionsScratch_.size() < capacity) {
        regionsScratch_.resize(capacity);
    }

    for (auto trackId : tracks) {
        if (auto* playlist = trackManager->getPlaylist(trackId)) {
            uint32_t count = playlist->getAllRegions(regionsScratch_.data(), static_cast<uint32_t>(regionsScratch_.size()));
            while (count == regionsScratch_.size()) {
                regionsScratch_.resize(regionsScratch_.size() * 2);
                count = playlist->getAllRegions(regionsScratch_.data(), static_cast<uint32_t>(regionsScratch_.size()));
            }
            for (uint32_t i = 0; i < count; ++i) {
                if (regionsScratch_[i].id.id == regionId.id &&
                    (regionId.generation == 0 || regionsScratch_[i].id.generation == regionId.generation)) {
                    outInfo = regionsScratch_[i];
                    outTrackId = trackId;
                    return playlist;
                }
            }
        }


    }
    return nullptr;
}

void ArrangementController::deleteRegion(RegionID regionId) {
    auto* trackManager = getTrackManager();
    if (!trackManager) return;

    auto* session = sessionManager_ ? sessionManager_->getActiveSession() : nullptr;
    auto* history = session ? session->getCommandHistory() : nullptr;
    if (history) {
        history->beginCompound();
    }

    composition::IPlaylist::RegionInfo info{};
    TrackID trackId = TrackID::invalid();
    if (auto* playlist = findPlaylistForRegion(regionId, info, trackId)) {
        playlist->removeRegion(regionId);
        if (info.region.type == composition::RegionType::MIDI) {
            composition::ClipID clipId = composition::ClipID::fromRaw(info.region.sourceId.toRaw());
            if (auto* seq = trackManager->getMIDISequencer(trackId)) {
                seq->removeClip(clipId);
            }
        }
        
        if (session && session->getRegionMetadataManager()) {
            session->getRegionMetadataManager()->removeRegionMetadata(regionId, true);
        }

        trackManager->compileTimelineSnapshot();
    }

    if (history) {
        history->endCompound();
    }
}

void ArrangementController::splitRegion(RegionID regionId, uint64_t splitFrame) {
    auto* trackManager = getTrackManager();
    if (!trackManager) return;

    composition::IPlaylist::RegionInfo oldRegionInfo{};
    TrackID trackId = TrackID::invalid();
    if (auto* playlist = findPlaylistForRegion(regionId, oldRegionInfo, trackId)) {
        uint64_t oldPosition = oldRegionInfo.region.positionSample;
        uint64_t sourceOffset = 0;
        if (oldRegionInfo.region.type == composition::RegionType::AUDIO) {
            uint64_t projectOffset = splitFrame - oldPosition;
            double srRatio = 1.0;
            if (auto* sourceManager = getSourceManager()) {
                composition::AudioSourceDescriptor desc;
                if (sourceManager->getSource(oldRegionInfo.region.sourceId, desc) && desc.sampleRate > 0) {
                    uint32_t projectSampleRate = 44100;
                    if (sessionManager_) {
                        if (auto* session = sessionManager_->getActiveSession()) {
                            uint32_t rate = session->getMetadata().sampleRate;
                            if (rate > 0) {
                                projectSampleRate = rate;
                            }
                        }
                    }
                    srRatio = static_cast<double>(desc.sampleRate) / static_cast<double>(projectSampleRate);
                }
            }
            double ratio = (oldRegionInfo.region.playbackRatio > 0.0f ? static_cast<double>(oldRegionInfo.region.playbackRatio) : 1.0) * srRatio;
            sourceOffset = static_cast<uint64_t>(static_cast<double>(projectOffset) * ratio);
        } else {
            sourceOffset = splitFrame - oldPosition;
        }
        RegionID rightId = playlist->splitRegion(regionId, splitFrame, sourceOffset);
        
        if (oldRegionInfo.region.type == composition::RegionType::MIDI && rightId.isValid()) {
            composition::IPlaylist::RegionInfo rightInfo{};
            bool foundRight = false;
            uint32_t rCount = playlist->getAllRegions(regionsScratch_.data(), static_cast<uint32_t>(regionsScratch_.size()));
            for (uint32_t j = 0; j < rCount; ++j) {
                if (regionsScratch_[j].id == rightId) {
                    rightInfo = regionsScratch_[j];
                    foundRight = true;
                    break;
                }
            }
            
            if (foundRight) {
                composition::ClipID oldClipId = composition::ClipID::fromRaw(oldRegionInfo.region.sourceId.toRaw());
                composition::ClipID newClipId = composition::ClipID::fromRaw(rightInfo.region.sourceId.toRaw());
                
                if (auto* seq = trackManager->getMIDISequencer(trackId)) {
                    seq->updateClipPosition(newClipId, rightInfo.region.positionSample, rightInfo.region.sourceLength);
                    
                    std::vector<composition::MIDINote> clipNotes(4096);
                    uint32_t noteCount = seq->getNotesInClip(oldClipId, clipNotes.data(), 4096);
                    uint64_t offset = splitFrame - oldPosition;
                    
                    for (uint32_t n = 0; n < noteCount; ++n) {
                        const auto& note = clipNotes[n];
                        if (note.offsetSample >= offset) {
                            composition::MIDINote migratedNote = note;
                            migratedNote.offsetSample = note.offsetSample - offset;
                            seq->addNote(newClipId, migratedNote);
                            seq->removeNote(note.noteId);
                        } else if (note.offsetSample + note.durationSample > offset) {
                            composition::MIDINote truncatedNote = note;
                            truncatedNote.durationSample = offset - note.offsetSample;
                            seq->updateNote(note.noteId, truncatedNote);
                        }
                    }
                    
                    std::vector<composition::MIDICCPoint> clipCC(8192);
                    uint32_t ccCount = seq->getCCPointsInClip(oldClipId, clipCC.data(), 8192);
                    seq->removeCCPointsInClip(oldClipId);
                    
                    for (uint32_t c = 0; c < ccCount; ++c) {
                        if (clipCC[c].samplePosition >= splitFrame) {
                            seq->addCCPoint(newClipId, clipCC[c]);
                        } else {
                            seq->addCCPoint(oldClipId, clipCC[c]);
                        }
                    }
                }
            }
        }

        // Unconditional metadata copy on split
        auto* session = sessionManager_ ? sessionManager_->getActiveSession() : nullptr;
        if (session && session->getRegionMetadataManager() && rightId.isValid()) {
            composition::RegionMetadata parentMeta{};
            session->getRegionMetadataManager()->getRegionMetadata(regionId, parentMeta);
            session->getRegionMetadataManager()->setRegionMetadata(rightId, parentMeta, true);
        }

        trackManager->compileTimelineSnapshot();
    }
}

RegionID ArrangementController::moveRegion(RegionID regionId, TrackID destTrack, int64_t newStartFrame, uint32_t destLayer) {
    auto* trackManager = getTrackManager();
    if (!trackManager) return {0, 0};

    const uint64_t safeStartFrame = (newStartFrame < 0) ? 0ULL : static_cast<uint64_t>(newStartFrame);

    composition::IPlaylist::RegionInfo targetInfo{};
    TrackID srcTrack = TrackID::invalid();
    composition::IPlaylist* srcPlaylist = findPlaylistForRegion(regionId, targetInfo, srcTrack);

    if (!srcPlaylist) return {0, 0};

    composition::IPlaylist* destPlaylist = trackManager->getPlaylist(destTrack);

    if (!destPlaylist) return {0, 0};

    if (srcTrack == destTrack) {
        srcPlaylist->moveRegion(regionId, safeStartFrame, destLayer == 0xFFFFFFFF ? targetInfo.layer : destLayer);
        if (targetInfo.region.type == composition::RegionType::MIDI) {
            composition::ClipID clipId = composition::ClipID::fromRaw(targetInfo.region.sourceId.toRaw());
            if (auto* seq = trackManager->getMIDISequencer(srcTrack)) {
                seq->updateClipPosition(clipId, safeStartFrame, targetInfo.region.sourceLength);
            }
        }
        trackManager->compileTimelineSnapshot();
        return regionId;
    } else {
        bool isAudio = (targetInfo.region.type == composition::RegionType::AUDIO);
        auto* sourceManager = getSourceManager();
        if (isAudio && sourceManager) {
            sourceManager->incrementReference(targetInfo.region.sourceId);
        }

        srcPlaylist->removeRegion(regionId);
        
        targetInfo.region.positionSample = safeStartFrame;
        RegionID newRegId = destPlaylist->addRegion(targetInfo.region, destLayer == 0xFFFFFFFF ? composition::IPlaylist::AUTO_LAYER : destLayer);
        
        if (isAudio && sourceManager) {
            sourceManager->decrementReference(targetInfo.region.sourceId);
        }

        if (targetInfo.region.type == composition::RegionType::MIDI) {
            composition::ClipID clipId = composition::ClipID::fromRaw(targetInfo.region.sourceId.toRaw());
            auto* srcSeq = trackManager->getMIDISequencer(srcTrack);
            auto* destSeq = trackManager->getMIDISequencer(destTrack);
            if (srcSeq && destSeq) {
                std::vector<composition::MIDINote> clipNotes(4096);
                uint32_t noteCount = srcSeq->getNotesInClip(clipId, clipNotes.data(), 4096);
                for (uint32_t i = 0; i < noteCount; ++i) {
                    destSeq->addNote(clipId, clipNotes[i]);
                }
                
                std::vector<composition::MIDICCPoint> clipCC(8192);
                uint32_t ccCount = srcSeq->getCCPointsInClip(clipId, clipCC.data(), 8192);
                for (uint32_t i = 0; i < ccCount; ++i) {
                    destSeq->addCCPoint(clipId, clipCC[i]);
                }
                
                srcSeq->removeClip(clipId);
                destSeq->updateClipPosition(clipId, safeStartFrame, targetInfo.region.sourceLength);
            }
        }

        trackManager->compileTimelineSnapshot();
        return newRegId;
    }
}

void ArrangementController::trimRegion(RegionID regionId, uint64_t newPosition, uint64_t newSourceStart, uint64_t newDuration) {
    auto* trackManager = getTrackManager();
    if (!trackManager) return;

    composition::IPlaylist::RegionInfo info{};
    TrackID trackId = TrackID::invalid();
    if (auto* playlist = findPlaylistForRegion(regionId, info, trackId)) {
        double srRatio = 1.0; // file / project
        composition::AudioSourceDescriptor desc;
        bool hasSource = false;
        if (info.region.type == composition::RegionType::AUDIO) {
            auto* sourceManager = getSourceManager();
            if (sourceManager) {
                hasSource = sourceManager->getSource(info.region.sourceId, desc);
            }
        }
        if (hasSource && desc.sampleRate > 0) {
            uint32_t projectSampleRate = 44100;
            if (sessionManager_) {
                if (auto* session = sessionManager_->getActiveSession()) {
                    projectSampleRate = session->getMetadata().sampleRate;
                }
            }
            srRatio = static_cast<double>(desc.sampleRate) / static_cast<double>(projectSampleRate);
        }

        uint64_t fileSourceStart = static_cast<uint64_t>(static_cast<double>(newSourceStart) * srRatio);
        uint64_t fileSourceLength = static_cast<uint64_t>(static_cast<double>(newDuration) * srRatio);

        if (hasSource && info.region.type == composition::RegionType::AUDIO) {
            if (fileSourceStart > desc.totalLengthSamples) {
                fileSourceStart = desc.totalLengthSamples;
            }
            if (fileSourceStart + fileSourceLength > desc.totalLengthSamples) {
                fileSourceLength = desc.totalLengthSamples - fileSourceStart;
            }
        }

        playlist->trimRegion(regionId, newPosition, fileSourceStart, fileSourceLength);
        if (info.region.type == composition::RegionType::MIDI) {
            composition::ClipID clipId = composition::ClipID::fromRaw(info.region.sourceId.toRaw());
            if (auto* seq = trackManager->getMIDISequencer(trackId)) {
                seq->updateClipPosition(clipId, newPosition, newDuration);
            }
        }
        trackManager->compileTimelineSnapshot();
    }
}

void ArrangementController::setRegionGain(RegionID id, float gainLinear) {
    auto* trackManager = getTrackManager();
    if (!trackManager) return;
    composition::IPlaylist::RegionInfo info{};
    TrackID trackId = TrackID::invalid();
    if (auto* playlist = findPlaylistForRegion(id, info, trackId)) {
        playlist->setRegionGain(info.id, gainLinear);
        trackManager->compileTimelineSnapshot();
    }
}

void ArrangementController::setRegionFades(RegionID id, uint32_t fadeInFrames, uint32_t fadeOutFrames) {
    auto* trackManager = getTrackManager();
    if (!trackManager) return;
    composition::IPlaylist::RegionInfo info{};
    TrackID trackId = TrackID::invalid();
    if (auto* playlist = findPlaylistForRegion(id, info, trackId)) {
        playlist->setFades(info.id, fadeInFrames, fadeOutFrames);
        trackManager->compileTimelineSnapshot();
    }
}

void ArrangementController::setRegionMuted(RegionID id, bool muted) {
    auto* trackManager = getTrackManager();
    if (!trackManager) return;
    composition::IPlaylist::RegionInfo info{};
    TrackID trackId = TrackID::invalid();
    if (auto* playlist = findPlaylistForRegion(id, info, trackId)) {
        playlist->setRegionMuted(info.id, muted);
        trackManager->compileTimelineSnapshot();
    }
}

void ArrangementController::setRegionWarpMode(RegionID id, WarpMode mode) {
    auto* trackManager = getTrackManager();
    if (!trackManager) return;
    composition::IPlaylist::RegionInfo info{};
    TrackID trackId = TrackID::invalid();
    if (auto* playlist = findPlaylistForRegion(id, info, trackId)) {
        playlist->setWarpMode(info.id, mode);
        trackManager->compileTimelineSnapshot();
    }
}

void ArrangementController::setRegionPlaybackRatio(RegionID id, float ratio) {
    auto* trackManager = getTrackManager();
    if (!trackManager) return;
    composition::IPlaylist::RegionInfo info{};
    TrackID trackId = TrackID::invalid();
    if (auto* playlist = findPlaylistForRegion(id, info, trackId)) {
        playlist->setPlaybackRatio(info.id, ratio);
        trackManager->compileTimelineSnapshot();
    }
}

void ArrangementController::setRegionSourceBpm(RegionID id, float bpm) {
    auto* trackManager = getTrackManager();
    if (!trackManager) return;
    composition::IPlaylist::RegionInfo info{};
    TrackID trackId = TrackID::invalid();
    if (auto* playlist = findPlaylistForRegion(id, info, trackId)) {
        playlist->setSourceBpm(info.id, bpm);
        trackManager->compileTimelineSnapshot();
    }
}

bool ArrangementController::undo() {
    if (!sessionManager_) return false;
    auto* session = sessionManager_->getActiveSession();
    if (!session) return false;
    auto* history = session->getCommandHistory();
    if (!history) return false;
    bool success = history->undo();
    if (success) {
        sessionManager_->triggerSessionRefresh();
    }
    return success;
}

bool ArrangementController::redo() {
    if (!sessionManager_) return false;
    auto* session = sessionManager_->getActiveSession();
    if (!session) return false;
    auto* history = session->getCommandHistory();
    if (!history) return false;
    bool success = history->redo();
    if (success) {
        sessionManager_->triggerSessionRefresh();
    }
    return success;
}

void ArrangementController::consolidateTrack(TrackID trackId, uint64_t startFrame, uint64_t endFrame) {
    auto* trackManager = getTrackManager();
    if (!trackManager) return;
    auto* playlist = trackManager->getPlaylist(trackId);
    if (!playlist) return;

    // 1. Gather all regions on the track that overlap with the range
    std::vector<RegionID> regionsToDelete;
    uint32_t capacity = 512;
    if (regionsScratch_.size() < capacity) regionsScratch_.resize(capacity);
    uint32_t count = playlist->getAllRegions(regionsScratch_.data(), static_cast<uint32_t>(regionsScratch_.size()));
    while (count == regionsScratch_.size()) {
        regionsScratch_.resize(regionsScratch_.size() * 2);
        count = playlist->getAllRegions(regionsScratch_.data(), static_cast<uint32_t>(regionsScratch_.size()));
    }

    for (uint32_t i = 0; i < count; ++i) {
        const auto& rInfo = regionsScratch_[i];
        uint64_t rStart = rInfo.region.positionSample;
        uint64_t rEnd = rStart + rInfo.region.sourceLength;
        if (rStart < endFrame && rEnd > startFrame) {
            regionsToDelete.push_back(rInfo.id);
        }
    }

    if (regionsToDelete.empty()) return;

    auto* session = sessionManager_ ? sessionManager_->getActiveSession() : nullptr;
    auto* history = session ? session->getCommandHistory() : nullptr;
    if (history) {
        history->beginCompound();
    }

    // 2. Delete regions
    for (auto id : regionsToDelete) {
        playlist->removeRegion(id);
    }

    // 3. Insert new consolidated clip
    importAudioClip(trackId, "/cache/consolidated.wav", startFrame);

    if (history) {
        history->endCompound();
    }
}

uint64_t ArrangementController::getArrangementLength() const {
    auto* trackManager = getTrackManager();
    if (!trackManager) return 0;

    std::vector<TrackID> tracks = trackManager->getAllTrackIDs();
    uint64_t maxLength = 0;

    uint32_t capacity = 512;
    if (regionsScratch_.size() < capacity) {
        regionsScratch_.resize(capacity);
    }

    for (auto trackId : tracks) {
        if (auto* playlist = trackManager->getPlaylist(trackId)) {
            uint32_t count = playlist->getAllRegions(regionsScratch_.data(), static_cast<uint32_t>(regionsScratch_.size()));
            while (count == regionsScratch_.size()) {
                regionsScratch_.resize(regionsScratch_.size() * 2);
                count = playlist->getAllRegions(regionsScratch_.data(), static_cast<uint32_t>(regionsScratch_.size()));
            }
            for (uint32_t i = 0; i < count; ++i) {
                uint64_t endPos = regionsScratch_[i].region.positionSample + regionsScratch_[i].region.sourceLength;
                if (endPos > maxLength) {
                    maxLength = endPos;
                }
            }
        }
    }
    return maxLength;
}

void ArrangementController::mergePatternClips(TrackID trackId,
                                              uint64_t startFrame, uint64_t endFrame) {
    if (startFrame >= endFrame) return;

    auto* trackManager = getTrackManager();
    if (!trackManager) return;

    auto* playlist = trackManager->getPlaylist(trackId);
    if (!playlist) return;

    constexpr uint32_t MAX_REGIONS = 256;
    std::vector<composition::IPlaylist::RegionInfo> regionInfos(MAX_REGIONS);
    uint32_t count = playlist->getAllRegions(regionInfos.data(), MAX_REGIONS);
    regionInfos.resize(count);

    std::vector<composition::RegionID> regionsToMerge;
    for (const auto& info : regionInfos) {
        uint64_t rStart = info.region.positionSample;
        uint64_t rEnd = rStart + info.region.sourceLength;
        if (rStart < endFrame && rEnd > startFrame) {
            regionsToMerge.push_back(info.id);
        }
    }

    if (regionsToMerge.empty()) return;

    uint64_t mergeStart = startFrame;
    uint64_t mergeLength = endFrame - startFrame;

    for (const auto& regId : regionsToMerge) {
        playlist->removeRegion(regId);
    }

    composition::TimelineRegion mergedRegion{};
    mergedRegion.positionSample = mergeStart;
    mergedRegion.sourceStartSample = 0;
    mergedRegion.sourceLength = mergeLength;
    mergedRegion.gain = 1.0f;
    mergedRegion.isMuted = false;
    mergedRegion.warpMode = WarpMode::BYPASS;
    mergedRegion.playbackRatio = 1.0f;

    playlist->addRegion(mergedRegion);
}

void ArrangementController::autoNameClips(TrackID trackId) {
    auto* trackManager = getTrackManager();
    if (!trackManager || !stringRegistry_) return;

    composition::TrackCreateInfo trackInfo{};
    std::string trackName = "Track";
    if (trackManager->getTrackInfo(trackId, trackInfo)) {
        stringRegistry_->getString(trackInfo.nameId, trackName);
    }

    if (auto* playlist = trackManager->getPlaylist(trackId)) {
        uint32_t capacity = 512;
        if (regionsScratch_.size() < capacity) regionsScratch_.resize(capacity);
        uint32_t count = playlist->getAllRegions(regionsScratch_.data(),
                                                 static_cast<uint32_t>(regionsScratch_.size()));
        while (count == regionsScratch_.size()) {
            regionsScratch_.resize(regionsScratch_.size() * 2);
            count = playlist->getAllRegions(regionsScratch_.data(),
                                           static_cast<uint32_t>(regionsScratch_.size()));
        }
        auto* session = sessionManager_ ? sessionManager_->getActiveSession() : nullptr;
        for (uint32_t i = 0; i < count; ++i) {
            std::string customName = trackName + " Clip " + std::to_string(i + 1);
            if (session && session->getRegionMetadataManager()) {
                composition::RegionMetadata meta{};
                session->getRegionMetadataManager()->getRegionMetadata(regionsScratch_[i].id, meta);
                std::strncpy(meta.name, customName.c_str(), MAX_NAME_LENGTH - 1);
                meta.name[MAX_NAME_LENGTH - 1] = '\0';
                session->getRegionMetadataManager()->setRegionMetadata(regionsScratch_[i].id, meta, true);
            }
        }
    }
}

uint32_t ArrangementController::getRegionsInViewport(
    uint64_t viewStartFrame,
    uint64_t viewEndFrame,
    VisualRegion* outRegions,
    uint32_t maxCount
) const {
    auto* trackManager = getTrackManager();
    auto* sourceManager = getSourceManager();
    if (!trackManager || !sourceManager || !stringRegistry_ || !outRegions || maxCount == 0) {
        return 0;
    }

    uint32_t count = 0;
    std::vector<TrackID> tracks = trackManager->getAllTrackIDs();
    
    // Dynamically retrieve regions into scratch buffer without static caps
    uint32_t capacity = 512;
    if (regionsScratch_.size() < capacity) {
        regionsScratch_.resize(capacity);
    }

    for (auto trackId : tracks) {
        if (auto* playlist = trackManager->getPlaylist(trackId)) {
            uint32_t rCount = playlist->getAllRegions(regionsScratch_.data(), static_cast<uint32_t>(regionsScratch_.size()));
            while (rCount == regionsScratch_.size()) {
                regionsScratch_.resize(regionsScratch_.size() * 2);
                rCount = playlist->getAllRegions(regionsScratch_.data(), static_cast<uint32_t>(regionsScratch_.size()));
            }
            for (uint32_t i = 0; i < rCount; ++i) {
                const auto& info = regionsScratch_[i];
                uint64_t regStart = info.region.positionSample;
                uint64_t regEnd = regStart + info.region.sourceLength;

                // Overlap check
                if (regStart < viewEndFrame && regEnd > viewStartFrame) {
                    if (count < maxCount) {
                        VisualRegion& vr = outRegions[count];
                        vr.id = info.id;
                        vr.trackId = trackId;
                        // Override visual clip type based on the actual region content type
                        if (info.region.type == composition::RegionType::MIDI) {
                            vr.clipType = composition::RegionType::MIDI;
                        } else {
                            vr.clipType = composition::RegionType::AUDIO;
                        }

                        vr.automationTargetNodeId = NodeID::invalid();
                        vr.automationParameterIndex = 0;

                        // Unconditionally retrieve fields from RegionMetadataManager
                        auto* session = sessionManager_->getActiveSession();
                        composition::RegionMetadata metadata{};
                        if (session && session->getRegionMetadataManager()) {
                            session->getRegionMetadataManager()->getRegionMetadata(info.id, metadata);
                        }

                        std::strncpy(vr.name, metadata.name, sizeof(vr.name) - 1);
                        vr.name[sizeof(vr.name) - 1] = '\0';
                        std::strncpy(vr.comment, metadata.comment, sizeof(vr.comment) - 1);
                        vr.comment[sizeof(vr.comment) - 1] = '\0';
                        vr.colorARGB = metadata.colorARGB;
                        vr.hasCustomComment = metadata.hasComment;

                        // Query source descriptor exactly once to obtain file path & mediaId
                        composition::AudioSourceDescriptor desc;
                        bool hasSource = sourceManager->getSource(info.region.sourceId, desc);

                        uint32_t srcSampleRate = 44100;
                        uint32_t projSampleRate = 44100;
                        if (session) {
                            projSampleRate = session->getMetadata().sampleRate;
                        }
                        if (hasSource && info.region.type == composition::RegionType::AUDIO) {
                            srcSampleRate = desc.sampleRate;
                        } else {
                            srcSampleRate = projSampleRate;
                        }

                        double srRatio = 1.0;
                        if (srcSampleRate > 0 && projSampleRate > 0) {
                            srRatio = static_cast<double>(projSampleRate) / static_cast<double>(srcSampleRate);
                        }

                        vr.startFrame = regStart;
                        vr.durationFrames = static_cast<uint64_t>(static_cast<double>(info.region.sourceLength) * srRatio);
                        // Convert file offset to project frames for the UI representation
                        vr.fileOffsetFrames = static_cast<uint64_t>(static_cast<double>(info.region.sourceStartSample) * srRatio);
                        vr.isSelected = false; // Presentation state, defaulted to false
                        vr.isMuted = info.region.isMuted;
                        vr.gainLinear = info.region.gain;
                        vr.fadeInFrames = info.region.fadeInSamples;
                        vr.fadeOutFrames = info.region.fadeOutSamples;
                        vr.mediaId = hasSource ? desc.mediaId : 0;
                        vr.warpMode = info.region.warpMode;
                        vr.playbackRatio = info.region.playbackRatio;
                        vr.sourceBpm = info.region.sourceBpm;
                        vr.timelineToSourceRatio = (info.region.playbackRatio > 0.0f ? static_cast<double>(info.region.playbackRatio) : 1.0) * (static_cast<double>(srcSampleRate) / static_cast<double>(projSampleRate > 0 ? projSampleRate : 44100));
                        if (info.region.type == composition::RegionType::AUDIO && hasSource) {
                            vr.sourceLengthFrames = static_cast<uint64_t>(static_cast<double>(desc.totalLengthSamples) * srRatio);
                        } else {
                            vr.sourceLengthFrames = ~0ULL;
                        }
                        vr.layerIndex = info.layer;

                        count++;
                    } else {
                        break;
                    }
                }
            }
        }


    }
    return count;
}

uint32_t ArrangementController::getActiveRecordings(VisualActiveRecording* outRecordings, uint32_t maxCount) {
    if (!recordingController_) return 0;
    std::vector<IRecordingController::ActiveRecordingInfo> infos(maxCount);
    uint32_t count = recordingController_->getActiveRecordings(infos.data(), maxCount);
    for (uint32_t i = 0; i < count; ++i) {
        outRecordings[i].trackId = infos[i].trackId;
        outRecordings[i].startFrame = infos[i].startSample;
        outRecordings[i].currentFrame = infos[i].currentSample;
        outRecordings[i].livePeaks = infos[i].livePeaks;
        outRecordings[i].numPeaks = infos[i].numPeaks;
    }
    return count;
}

void ArrangementController::swapTakeLayer(TrackID trackId, uint32_t laneIndex, uint64_t startFrame, uint64_t length)
{
    auto trackManager = getTrackManager();
    if (!trackManager) return;
    
    auto* playlist = trackManager->getPlaylist(trackId);
    if (!playlist) return;
    
    auto* session = sessionManager_->getActiveSession();
    if (!session) return;
    
    auto* compingEngine = session->getCompingEngine();
    if (!compingEngine) return;
    
    auto* history = session->getCommandHistory();
    if (history) {
        history->beginCompound();
    }
    
    compingEngine->swapTakeLayer(playlist, laneIndex, startFrame, length);
    
    if (history) {
        history->endCompound();
    }
}

bool ArrangementController::getVisualRegion(RegionID regionId, VisualRegion& outRegion) const {
    composition::IPlaylist::RegionInfo info{};
    TrackID trackId = TrackID::invalid();
    if (findPlaylistForRegion(regionId, info, trackId)) {
        outRegion.id = info.id;
        outRegion.trackId = trackId;
        outRegion.clipType = info.region.type;
        outRegion.startFrame = info.region.positionSample;
        uint32_t srcSampleRate = 44100;
        uint32_t projSampleRate = 44100;
        auto* session = sessionManager_->getActiveSession();
        if (session) {
            projSampleRate = session->getMetadata().sampleRate;
        }

        composition::AudioSourceDescriptor desc;
        bool hasSource = false;
        if (info.region.type == composition::RegionType::AUDIO) {
            auto* sourceManager = getSourceManager();
            if (sourceManager) {
                hasSource = sourceManager->getSource(info.region.sourceId, desc);
            }
        }

        if (hasSource) {
            srcSampleRate = desc.sampleRate;
        } else {
            srcSampleRate = projSampleRate;
        }

        double srRatio = 1.0;
        if (srcSampleRate > 0 && projSampleRate > 0) {
            srRatio = static_cast<double>(projSampleRate) / static_cast<double>(srcSampleRate);
        }

        outRegion.durationFrames = static_cast<uint64_t>(static_cast<double>(info.region.sourceLength) * srRatio);
        outRegion.fileOffsetFrames = static_cast<uint64_t>(static_cast<double>(info.region.sourceStartSample) * srRatio);
        outRegion.layerIndex = info.layer;
        outRegion.isMuted = info.region.isMuted;
        outRegion.gainLinear = info.region.gain;
        outRegion.fadeInFrames = info.region.fadeInSamples;
        outRegion.fadeOutFrames = info.region.fadeOutSamples;
        outRegion.warpMode = info.region.warpMode;
        outRegion.playbackRatio = info.region.playbackRatio;
        outRegion.sourceBpm = info.region.sourceBpm;
        outRegion.timelineToSourceRatio = (info.region.playbackRatio > 0.0f ? static_cast<double>(info.region.playbackRatio) : 1.0) * (static_cast<double>(srcSampleRate) / static_cast<double>(projSampleRate > 0 ? projSampleRate : 44100));
        if (info.region.type == composition::RegionType::AUDIO && hasSource) {
            outRegion.sourceLengthFrames = static_cast<uint64_t>(static_cast<double>(desc.totalLengthSamples) * srRatio);
        } else {
            outRegion.sourceLengthFrames = ~0ULL;
        }
        composition::RegionMetadata metadata{};
        if (session && session->getRegionMetadataManager()) {
            session->getRegionMetadataManager()->getRegionMetadata(info.id, metadata);
        }
        
        std::strncpy(outRegion.name, metadata.name, sizeof(outRegion.name) - 1);
        outRegion.name[sizeof(outRegion.name) - 1] = '\0';
        std::strncpy(outRegion.comment, metadata.comment, sizeof(outRegion.comment) - 1);
        outRegion.comment[sizeof(outRegion.comment) - 1] = '\0';
        outRegion.colorARGB = metadata.colorARGB;
        outRegion.hasCustomComment = metadata.hasComment;
        return true;
    }
    return false;
}

void ArrangementController::onSessionChanged(composition::IProjectSession* session) {
    if (!session || !mediaRegistry_ || !stringRegistry_) return;

    auto* sourceMgrRaw = session->getRegionSourceManager();
    auto* sourceMgr = dynamic_cast<composition::RegionSourceManagerImpl*>(sourceMgrRaw);
    if (!sourceMgr) return;

    auto sources = sourceMgr->getAllSources();
    for (const auto& src : sources) {
        std::string filePath = src.filePath;
        if (filePath.empty()) {
            stringRegistry_->getString(src.descriptor.nameId, filePath);
        }
        if (filePath.empty()) continue;

        MediaID mediaId = MediaID::fromRaw(src.descriptor.mediaId);
        
        MediaManagement::AssetInfo assetInfo{};
        assetInfo.mediaId = mediaId;
        assetInfo.pathId = src.descriptor.nameId;
        assetInfo.nameId = src.descriptor.nameId;
        assetInfo.durationSamples = src.descriptor.totalLengthSamples;
        assetInfo.sampleRate = src.descriptor.sampleRate;
        assetInfo.numChannels = static_cast<uint16_t>(src.descriptor.channelCount);
        assetInfo.sizeBytes = 0;
        assetInfo.bitDepth = 16;
        assetInfo.peakDecibels = 0.0f;
        assetInfo.rmsDecibels = 0.0f;
        assetInfo.importTime = 0;
        assetInfo.colorARGB = 0xFF5599FF;

        mediaRegistry_->registerAsset(assetInfo);

        // Trigger async waveform generation
        if (waveformRenderer_) {
            waveformRenderer_->getWaveform(mediaId, MediaManagement::WaveformResolution::OVERVIEW);
            waveformRenderer_->getWaveform(mediaId, MediaManagement::WaveformResolution::HIGH);
        }

        // Register file path in butler thread
        if (DSP::AudioSequencerFactory::s_butlerThread) {
            DSP::AudioSequencerFactory::s_butlerThread->registerSourcePath(src.descriptor.sourceId.id, filePath.c_str());
        }
    }
}

void ArrangementController::updateRegionMetadata(RegionID regionId, const char* name, const char* comment, uint32_t colorARGB) {
    auto* session = sessionManager_->getActiveSession();
    if (!session || !session->getRegionMetadataManager()) return;

    composition::RegionMetadata meta{};
    if (name) {
        std::strncpy(meta.name, name, MAX_NAME_LENGTH - 1);
        meta.name[MAX_NAME_LENGTH - 1] = '\0';
    } else {
        meta.name[0] = '\0';
    }

    if (comment) {
        std::strncpy(meta.comment, comment, MAX_COMMENT_LENGTH - 1);
        meta.comment[MAX_COMMENT_LENGTH - 1] = '\0';
        meta.hasComment = (std::strlen(comment) > 0);
    } else {
        meta.comment[0] = '\0';
        meta.hasComment = false;
    }

    meta.colorARGB = colorARGB;

    session->getRegionMetadataManager()->setRegionMetadata(regionId, meta, true);
}

void ArrangementController::initializeRegionMetadata(RegionID regionId, const char* defaultName, uint32_t colorARGB) {
    auto* session = sessionManager_->getActiveSession();
    if (!session || !session->getRegionMetadataManager()) return;

    composition::RegionMetadata meta{};
    std::strncpy(meta.name, defaultName ? defaultName : "Clip", MAX_NAME_LENGTH - 1);
    meta.name[MAX_NAME_LENGTH - 1] = '\0';
    meta.comment[0] = '\0';
    meta.colorARGB = colorARGB;
    meta.hasComment = false;

    session->getRegionMetadataManager()->setRegionMetadata(regionId, meta, true);
}

} // namespace bridge
