// src/musical_composition/recording/take_recording_intake.cpp
#include "take_recording_intake.h"
#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "musical_composition/playlist/iplaylist.h"
#include "musical_composition/region_manager/iaudio_region_source_manager.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "Core audio engine/streaming/ibutler_thread.h"
#include "DSP nodes/sequencer/audio_sequencer_node.h"

namespace composition {

TakeRecordingIntake::TakeRecordingIntake(Layer2::IStringRegistry* stringRegistry)
    : stringRegistry_(stringRegistry) {}

std::vector<RegionID> TakeRecordingIntake::ingestTake(const RawTake& take, 
                                                      uint64_t totalLatencySamples, 
                                                      const RecordingConfig& config,
                                                      IPlaylist* targetPlaylist,
                                                      IAudioRegionSourceManager* sourceManager) {
    std::vector<RegionID> createdRegions;
    if (!targetPlaylist || !sourceManager) return createdRegions;

    uint32_t nameId = stringRegistry_ ? stringRegistry_->registerString(take.filePath) : 0;
    
    AudioSourceDescriptor desc;
    desc.sourceId = SourceID::invalid();
    desc.nameId = nameId;
    desc.totalLengthSamples = take.hardwareEndSample - take.hardwareStartSample;
    desc.channelCount = take.channelCount;
    desc.sampleRate = take.sampleRate;
    desc.mediaId = take.mediaId;
    
    SourceID sourceId = sourceManager->registerSource(desc, take.filePath);

    TimelineRegion region{};
    region.type = RegionType::AUDIO;
    region.sourceId = sourceId;
    
    if (take.hardwareStartSample >= totalLatencySamples) {
        region.positionSample = take.hardwareStartSample - totalLatencySamples;
        region.sourceStartSample = 0;
    } else {
        region.positionSample = 0;
        region.sourceStartSample = totalLatencySamples - take.hardwareStartSample;
    }
    
    region.sourceLength = take.hardwareEndSample - take.hardwareStartSample;
    region.gain = 1.0f;
    region.isMuted = false;
    region.warpMode = WarpMode::BYPASS;
    region.playbackRatio = 1.0f;
    
    if (config.isLoopRecording) {
        uint32_t crossfadeSamples = 220; 
        if (region.sourceLength > crossfadeSamples * 2) {
            region.fadeInSamples = crossfadeSamples;
            region.fadeOutSamples = crossfadeSamples;
        } else {
            region.fadeInSamples = 0;
            region.fadeOutSamples = 0;
        }
    } else {
        region.fadeInSamples = 0;
        region.fadeOutSamples = 0;
    }

    RegionID newRegionId = targetPlaylist->addRegion(region);
    if (newRegionId.isValid()) {
        createdRegions.push_back(newRegionId);
    }

    if (DSP::AudioSequencerFactory::s_butlerThread) {
        DSP::AudioSequencerFactory::s_butlerThread->registerSourcePath(sourceId.id, take.filePath.c_str());
    }
    
    return createdRegions;
}

} // namespace composition
