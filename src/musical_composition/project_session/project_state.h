#pragma once

#include "iproject_session.h"
#include "musical_composition/playlist/iplaylist.h"
#include "musical_composition/musical_primitives.h"
#include "musical_composition/midi_sequencer/midi_sequencer_impl.h"
#include "musical_composition/interfaces/imarker_manager.h"
#include "ikey_signature_map.h"
#include "musical_composition/interfaces/iregion_metadata_manager.h"
#include <string>
#include <vector>
#include <utility>

namespace composition {

struct AudioSourceState {
    uint32_t id;
    uint32_t generation;
    uint32_t nameId;
    uint64_t totalLengthSamples;
    uint32_t channelCount;
    uint32_t sampleRate;
    uint64_t mediaId;
    std::string filePath;
    std::string relativeFilePath;
};

struct AutomationLaneState {
    uint8_t roleType;
    uint8_t slotIdx;
    uint32_t semanticNameId;
    uint32_t cachedParameterIndex;
    uint32_t subNodeId;
    std::vector<::AutomationPoint> points;
};

struct PlaylistRegionState {
    RegionID regionId;
    IPlaylist::LayerIndex layer;
    TimelineRegion region;
};

struct PluginState {
    uint32_t pluginId;
    bool bypassed;
    std::string name;
    std::vector<uint8_t> stateBlob;
};

struct TrackState {
    TrackID trackId;
    TrackType type;
    std::string name;
    uint32_t colorARGB;
    uint32_t audioChannelCount;
    bool isRecordArmed;
    bool isInputMonitoring;
    std::string comments;
    TrackID outputTargetTrackId;
    uint32_t inputSourceIndex;
    
    uint8_t automationMode;
    std::vector<AutomationLaneState> automationLanes;
    
    bool hasPlaylist = false;
    std::vector<PlaylistRegionState> playlistRegions;

    bool hasSequencer = false;
    std::vector<MIDISequencerImpl::ClipPositionEntry> clipPositions;
    std::vector<MIDISequencerImpl::NoteEntry> notes;
    std::vector<MIDISequencerImpl::CCEntry> ccPoints;
    std::vector<MIDISequencerImpl::PitchEntry> pitchPoints;

    bool hasInstrument = false;
    PluginState instrument;

    std::vector<std::pair<uint32_t, PluginState>> inserts; // pair of slot index and plugin state

    struct SidechainRoutingState {
        uint32_t slotIndex{0};
        TrackID sourceTrackId{TrackID::invalid()};
        float sendGainLinear{1.0f};
    };
    std::vector<SidechainRoutingState> sidechains;
};

struct RegionMetadataState {
    RegionID regionId;
    RegionMetadata metadata;
};

struct ProjectState {
    ProjectMetadata metadata;
    std::vector<AudioSourceState> sources;
    std::vector<TrackState> tracks;
    std::vector<MarkerInfo> markers;
    std::vector<KeySignaturePoint> keySignatures;
    std::vector<RegionMetadataState> regionMetadata;
    MixStatistics mixStats;
};

} // namespace composition
