// src/Middle Bridge/pattern_data_provider.cpp
#include "Middle Bridge/telemetry/pattern_data_provider.h"
#include "musical_composition/midi_sequencer/imidi_sequencer.h"
#include "musical_composition/playlist/iplaylist.h"
#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/track_manager/itrack_manager.h"

namespace bridge {

PatternDataProvider::PatternDataProvider(ISessionManager *sessionManager)
    : sessionManager_(sessionManager) {}

uint32_t PatternDataProvider::getNoteEventsForRegion(RegionID regionId,
                                                     VisualNoteEvent *outEvents,
                                                     uint32_t maxCount) const {
  if (!outEvents || maxCount == 0 || !sessionManager_) {
    return 0;
  }

  auto *session = sessionManager_->getActiveSession();
  if (!session) {
    return 0;
  }

  auto *trackManager = session->getTrackManager();
  if (!trackManager) {
    return 0;
  }

  std::vector<TrackID> tracks = trackManager->getAllTrackIDs();
  for (auto trackId : tracks) {
    auto *playlist = trackManager->getPlaylist(trackId);
    if (!playlist) {
      continue;
    }

    // Query regions of this track
    std::vector<composition::IPlaylist::RegionInfo> regions(128);
    uint32_t regionCount = playlist->getAllRegions(regions.data(), 128);
    regionCount = std::min(regionCount, 128u);

    for (uint32_t i = 0; i < regionCount; ++i) {
      if (regions[i].id == regionId) {
        // Found the region! Now get the MIDI sequencer of this track
        auto *sequencer = trackManager->getMIDISequencer(trackId);
        if (!sequencer) {
          return 0;
        }

        // Get the start sample
        uint64_t startSample = regions[i].region.positionSample;

        // Get midi notes in this clip
        composition::ClipID clipId =
            composition::ClipID::fromRaw(regions[i].region.sourceId.toRaw());
        std::vector<composition::MIDINote> notes(maxCount);
        uint32_t noteCount =
            sequencer->getNotesInClip(clipId, notes.data(), maxCount);
        noteCount = std::min(noteCount, maxCount);

        // Convert MIDINote to VisualNoteEvent
        for (uint32_t j = 0; j < noteCount; ++j) {
          outEvents[j].startFrame = startSample + notes[j].offsetSample;
          outEvents[j].durationFrames =
              static_cast<uint32_t>(notes[j].durationSample);
          outEvents[j].pitch = notes[j].pitch;
          outEvents[j].velocity = notes[j].velocity;
        }
        return noteCount;
      }
    }
  }

  return 0;
}

} // namespace bridge
