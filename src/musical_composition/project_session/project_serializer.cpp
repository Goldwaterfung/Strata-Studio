#include "project_serializer.h"
#include <cstring>

namespace composition {

// =============================================================================
// Binary write helpers
// =============================================================================

void ProjectSerializer::writeBytes(std::vector<uint8_t>& buf,
                                    const void* data, size_t n) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
    buf.insert(buf.end(), p, p + n);
}

void ProjectSerializer::writeString(std::vector<uint8_t>& buf,
                                     const std::string& s) {
    uint32_t len = static_cast<uint32_t>(s.size());
    writeT(buf, len);
    writeBytes(buf, s.data(), len);
}

// =============================================================================
// Binary read helpers
// =============================================================================

bool ProjectSerializer::readBytes(const std::vector<uint8_t>& buf,
                                   size_t& offset, void* out, size_t n) {
    if (offset + n > buf.size()) return false;
    std::memcpy(out, buf.data() + offset, n);
    offset += n;
    return true;
}

bool ProjectSerializer::readString(const std::vector<uint8_t>& buf,
                                    size_t& offset, std::string& s) {
    uint32_t len = 0;
    if (!readT(buf, offset, len)) return false;
    if (offset + len > buf.size()) return false;
    s.assign(reinterpret_cast<const char*>(buf.data() + offset), len);
    offset += len;
    return true;
}

// =============================================================================
// Serialize
// =============================================================================

bool ProjectSerializer::serialize(
    const ProjectState& state,
    std::vector<uint8_t>& outBuffer
) {
    outBuffer.clear();

    // ---- Header ----
    const char* header = "AGDAW_PROJ_V6";
    writeBytes(outBuffer, header, std::strlen(header));

    // ---- Metadata ----
    writeString(outBuffer, state.metadata.projectName);
    writeString(outBuffer, state.metadata.author);
    writeT(outBuffer, state.metadata.sampleRate);
    writeT(outBuffer, state.metadata.initialTempoBPM);
    writeT(outBuffer, state.metadata.timeSignatureNumerator);
    writeT(outBuffer, state.metadata.timeSignatureDenominator);
    writeT(outBuffer, state.metadata.targetBitDepth);
    writeT(outBuffer, state.metadata.sessionDurationSeconds);

    // ---- Global Sources ----
    writeT(outBuffer, static_cast<uint32_t>(state.sources.size()));
    for (const auto& src : state.sources) {
        writeT(outBuffer, src.id);
        writeT(outBuffer, src.generation);
        writeT(outBuffer, src.nameId);
        writeT(outBuffer, src.totalLengthSamples);
        writeT(outBuffer, src.channelCount);
        writeT(outBuffer, src.sampleRate);
        writeT(outBuffer, src.mediaId);
        writeString(outBuffer, src.filePath);
        writeString(outBuffer, src.relativeFilePath);
    }

    // ---- Tracks ----
    writeT(outBuffer, static_cast<uint32_t>(state.tracks.size()));
    for (const auto& track : state.tracks) {
        writeT(outBuffer, track.trackId.id);
        writeT(outBuffer, track.trackId.generation);
        writeT(outBuffer, static_cast<uint8_t>(track.type));
        writeString(outBuffer, track.name);
        writeT(outBuffer, track.colorARGB);
        writeT(outBuffer, track.audioChannelCount);
        writeT(outBuffer, static_cast<uint8_t>(track.isRecordArmed ? 1 : 0));
        writeT(outBuffer, static_cast<uint8_t>(track.isInputMonitoring ? 1 : 0));
        writeString(outBuffer, track.comments);
        writeT(outBuffer, track.outputTargetTrackId.id);
        writeT(outBuffer, track.outputTargetTrackId.generation);
        writeT(outBuffer, track.inputSourceIndex);
        writeT(outBuffer, track.automationMode);

        // Automation Lanes
        writeT(outBuffer, static_cast<uint32_t>(track.automationLanes.size()));
        for (const auto& lane : track.automationLanes) {
            writeT(outBuffer, lane.roleType);
            writeT(outBuffer, lane.slotIdx);
            writeT(outBuffer, lane.semanticNameId);
            writeT(outBuffer, lane.cachedParameterIndex);
            writeT(outBuffer, lane.subNodeId);

            // Points
            writeT(outBuffer, static_cast<uint32_t>(lane.points.size()));
            for (const auto& pt : lane.points) {
                writeT(outBuffer, pt.positionSample);
                writeT(outBuffer, pt.value);
                writeT(outBuffer, static_cast<uint8_t>(pt.curveShape));
                writeT(outBuffer, pt.tension);
            }
        }

        // Playlist Regions
        writeT(outBuffer, static_cast<uint8_t>(track.hasPlaylist ? 1 : 0));
        if (track.hasPlaylist) {
            writeT(outBuffer, static_cast<uint32_t>(track.playlistRegions.size()));
            for (const auto& entry : track.playlistRegions) {
                writeT(outBuffer, entry.regionId.id);
                writeT(outBuffer, entry.regionId.generation);
                writeT(outBuffer, entry.layer);
                writeBytes(outBuffer, &entry.region, sizeof(TimelineRegion));
            }
        }

        // MIDI Sequencer
        writeT(outBuffer, static_cast<uint8_t>(track.hasSequencer ? 1 : 0));
        if (track.hasSequencer) {
            writeT(outBuffer, static_cast<uint32_t>(track.clipPositions.size()));
            for (const auto& cp : track.clipPositions) {
                writeBytes(outBuffer, &cp, sizeof(MIDISequencerImpl::ClipPositionEntry));
            }

            writeT(outBuffer, static_cast<uint32_t>(track.notes.size()));
            for (const auto& note : track.notes) {
                writeBytes(outBuffer, &note, sizeof(MIDISequencerImpl::NoteEntry));
            }

            writeT(outBuffer, static_cast<uint32_t>(track.ccPoints.size()));
            for (const auto& cc : track.ccPoints) {
                writeBytes(outBuffer, &cc, sizeof(MIDISequencerImpl::CCEntry));
            }

            writeT(outBuffer, static_cast<uint32_t>(track.pitchPoints.size()));
            for (const auto& pp : track.pitchPoints) {
                writeBytes(outBuffer, &pp, sizeof(MIDISequencerImpl::PitchEntry));
            }
        }

        // Instrument Plugin
        writeT(outBuffer, static_cast<uint8_t>(track.hasInstrument ? 1 : 0));
        if (track.hasInstrument) {
            writeT(outBuffer, track.instrument.pluginId);
            writeT(outBuffer, static_cast<uint8_t>(track.instrument.bypassed ? 1 : 0));
            writeString(outBuffer, track.instrument.name);
            writeT(outBuffer, static_cast<uint32_t>(track.instrument.stateBlob.size()));
            writeBytes(outBuffer, track.instrument.stateBlob.data(), track.instrument.stateBlob.size());
        }

        // Insert Plugins
        writeT(outBuffer, static_cast<uint32_t>(track.inserts.size()));
        for (const auto& insert : track.inserts) {
            writeT(outBuffer, insert.first); // slotIdx
            writeT(outBuffer, insert.second.pluginId);
            writeT(outBuffer, static_cast<uint8_t>(insert.second.bypassed ? 1 : 0));
            writeString(outBuffer, insert.second.name);
            writeT(outBuffer, static_cast<uint32_t>(insert.second.stateBlob.size()));
            writeBytes(outBuffer, insert.second.stateBlob.data(), insert.second.stateBlob.size());
        }
    }

    // ---- Markers ----
    writeT(outBuffer, static_cast<uint32_t>(state.markers.size()));
    for (const auto& marker : state.markers) {
        writeBytes(outBuffer, marker.uuid.bytes, 16);
        writeT(outBuffer, marker.framePosition);
        writeBytes(outBuffer, marker.label, MAX_NAME_LENGTH);
        writeT(outBuffer, marker.colorARGB);
    }

    // ---- Key Signatures ----
    writeT(outBuffer, static_cast<uint32_t>(state.keySignatures.size()));
    for (const auto& ev : state.keySignatures) {
        writeT(outBuffer, ev.positionSample);
        writeT(outBuffer, static_cast<uint8_t>(ev.rootNote));
        writeT(outBuffer, static_cast<uint8_t>(ev.type));
    }

    // ---- Mix Statistics ----
    writeT(outBuffer, static_cast<uint8_t>(state.mixStats.isAnalyzed ? 1 : 0));
    if (state.mixStats.isAnalyzed) {
        writeT(outBuffer, state.mixStats.integratedLoudnessLUFS);
        writeT(outBuffer, state.mixStats.truePeakDBTP);
        writeT(outBuffer, static_cast<uint8_t>(state.mixStats.clippingDetected ? 1 : 0));
    }

    // ---- Region Metadata ----
    writeT(outBuffer, static_cast<uint32_t>(state.regionMetadata.size()));
    for (const auto& item : state.regionMetadata) {
        writeT(outBuffer, item.regionId.toRaw());
        writeBytes(outBuffer, item.metadata.name, MAX_NAME_LENGTH);
        writeBytes(outBuffer, item.metadata.comment, MAX_COMMENT_LENGTH);
        writeT(outBuffer, item.metadata.colorARGB);
        writeT(outBuffer, static_cast<uint8_t>(item.metadata.hasComment ? 1 : 0));
    }

    return true;
}

// =============================================================================
// Deserialize
// =============================================================================

bool ProjectSerializer::deserialize(
    const std::vector<uint8_t>& inBuffer,
    ProjectState& outState
) {
    size_t offset = 0;

    // ---- Header ----
    const size_t headerLen = 13;
    if (inBuffer.size() < headerLen) return false;
    if (std::memcmp(inBuffer.data(), "AGDAW_PROJ_V6", headerLen) != 0) {
        return false;
    }
    offset += headerLen;

    // ---- Metadata ----
    if (!readString(inBuffer, offset, outState.metadata.projectName)) return false;
    if (!readString(inBuffer, offset, outState.metadata.author))      return false;
    if (!readT(inBuffer, offset, outState.metadata.sampleRate))               return false;
    if (!readT(inBuffer, offset, outState.metadata.initialTempoBPM))          return false;
    if (!readT(inBuffer, offset, outState.metadata.timeSignatureNumerator))   return false;
    if (!readT(inBuffer, offset, outState.metadata.timeSignatureDenominator)) return false;
    if (!readT(inBuffer, offset, outState.metadata.targetBitDepth))           return false;
    if (!readT(inBuffer, offset, outState.metadata.sessionDurationSeconds))   return false;

    // ---- Global Sources ----
    uint32_t sourceCount = 0;
    if (!readT(inBuffer, offset, sourceCount)) return false;
    outState.sources.resize(sourceCount);
    for (uint32_t i = 0; i < sourceCount; ++i) {
        auto& src = outState.sources[i];
        if (!readT(inBuffer, offset, src.id)) return false;
        if (!readT(inBuffer, offset, src.generation)) return false;
        if (!readT(inBuffer, offset, src.nameId)) return false;
        if (!readT(inBuffer, offset, src.totalLengthSamples)) return false;
        if (!readT(inBuffer, offset, src.channelCount)) return false;
        if (!readT(inBuffer, offset, src.sampleRate)) return false;
        if (!readT(inBuffer, offset, src.mediaId)) return false;
        if (!readString(inBuffer, offset, src.filePath)) return false;
        if (!readString(inBuffer, offset, src.relativeFilePath)) return false;
    }

    // ---- Tracks ----
    uint32_t trackCount = 0;
    if (!readT(inBuffer, offset, trackCount)) return false;
    outState.tracks.resize(trackCount);

    for (uint32_t t = 0; t < trackCount; ++t) {
        auto& track = outState.tracks[t];
        if (!readT(inBuffer, offset, track.trackId.id))         return false;
        if (!readT(inBuffer, offset, track.trackId.generation)) return false;

        uint8_t typeByte = 0;
        if (!readT(inBuffer, offset, typeByte)) return false;
        track.type = static_cast<TrackType>(typeByte);

        if (!readString(inBuffer, offset, track.name))            return false;
        if (!readT(inBuffer, offset, track.colorARGB))            return false;
        if (!readT(inBuffer, offset, track.audioChannelCount))    return false;

        uint8_t recordArmed = 0, inputMon = 0;
        if (!readT(inBuffer, offset, recordArmed)) return false;
        if (!readT(inBuffer, offset, inputMon))    return false;
        track.isRecordArmed     = (recordArmed != 0);
        track.isInputMonitoring = (inputMon    != 0);

        if (!readString(inBuffer, offset, track.comments))                  return false;
        if (!readT(inBuffer, offset, track.outputTargetTrackId.id))         return false;
        if (!readT(inBuffer, offset, track.outputTargetTrackId.generation)) return false;
        if (!readT(inBuffer, offset, track.inputSourceIndex))               return false;
        if (!readT(inBuffer, offset, track.automationMode))                 return false;

        // Automation Lanes
        uint32_t laneCount = 0;
        if (!readT(inBuffer, offset, laneCount)) return false;
        track.automationLanes.resize(laneCount);

        for (uint32_t l = 0; l < laneCount; ++l) {
            auto& lane = track.automationLanes[l];
            if (!readT(inBuffer, offset, lane.roleType))             return false;
            if (!readT(inBuffer, offset, lane.slotIdx))              return false;
            if (!readT(inBuffer, offset, lane.semanticNameId))        return false;
            if (!readT(inBuffer, offset, lane.cachedParameterIndex))  return false;
            if (!readT(inBuffer, offset, lane.subNodeId))            return false;

            uint32_t pointCount = 0;
            if (!readT(inBuffer, offset, pointCount)) return false;
            lane.points.resize(pointCount);

            for (uint32_t p = 0; p < pointCount; ++p) {
                auto& pt = lane.points[p];
                uint8_t shapeByte = 0;
                if (!readT(inBuffer, offset, pt.positionSample)) return false;
                if (!readT(inBuffer, offset, pt.value))          return false;
                if (!readT(inBuffer, offset, shapeByte))         return false;
                if (!readT(inBuffer, offset, pt.tension))        return false;
                pt.curveShape = static_cast<::AutomationPoint::Shape>(shapeByte);
            }
        }

        // Playlist Regions
        uint8_t hasPlaylist = 0;
        if (!readT(inBuffer, offset, hasPlaylist)) return false;
        track.hasPlaylist = (hasPlaylist != 0);
        if (track.hasPlaylist) {
            uint32_t regionCount = 0;
            if (!readT(inBuffer, offset, regionCount)) return false;
            track.playlistRegions.resize(regionCount);
            for (uint32_t r = 0; r < regionCount; ++r) {
                auto& entry = track.playlistRegions[r];
                if (!readT(inBuffer, offset, entry.regionId.id)) return false;
                if (!readT(inBuffer, offset, entry.regionId.generation)) return false;
                if (!readT(inBuffer, offset, entry.layer)) return false;
                if (!readBytes(inBuffer, offset, &entry.region, sizeof(TimelineRegion))) return false;
            }
        }

        // MIDI Sequencer
        uint8_t hasSequencer = 0;
        if (!readT(inBuffer, offset, hasSequencer)) return false;
        track.hasSequencer = (hasSequencer != 0);
        if (track.hasSequencer) {
            uint32_t clipPosCount = 0;
            if (!readT(inBuffer, offset, clipPosCount)) return false;
            track.clipPositions.resize(clipPosCount);
            for (uint32_t c = 0; c < clipPosCount; ++c) {
                if (!readBytes(inBuffer, offset, &track.clipPositions[c], sizeof(MIDISequencerImpl::ClipPositionEntry))) return false;
            }

            uint32_t notesCount = 0;
            if (!readT(inBuffer, offset, notesCount)) return false;
            track.notes.resize(notesCount);
            for (uint32_t n = 0; n < notesCount; ++n) {
                if (!readBytes(inBuffer, offset, &track.notes[n], sizeof(MIDISequencerImpl::NoteEntry))) return false;
            }

            uint32_t ccCount = 0;
            if (!readT(inBuffer, offset, ccCount)) return false;
            track.ccPoints.resize(ccCount);
            for (uint32_t c = 0; c < ccCount; ++c) {
                if (!readBytes(inBuffer, offset, &track.ccPoints[c], sizeof(MIDISequencerImpl::CCEntry))) return false;
            }

            uint32_t pitchCount = 0;
            if (!readT(inBuffer, offset, pitchCount)) return false;
            track.pitchPoints.resize(pitchCount);
            for (uint32_t p = 0; p < pitchCount; ++p) {
                if (!readBytes(inBuffer, offset, &track.pitchPoints[p], sizeof(MIDISequencerImpl::PitchEntry))) return false;
            }
        }

        // Instrument Plugin
        uint8_t hasInstrument = 0;
        if (!readT(inBuffer, offset, hasInstrument)) return false;
        track.hasInstrument = (hasInstrument != 0);
        if (track.hasInstrument) {
            auto& inst = track.instrument;
            uint8_t bypassedByte = 0;
            uint32_t chunkSize = 0;
            
            if (!readT(inBuffer, offset, inst.pluginId)) return false;
            if (!readT(inBuffer, offset, bypassedByte)) return false;
            inst.bypassed = (bypassedByte != 0);
            if (!readString(inBuffer, offset, inst.name)) return false;
            if (!readT(inBuffer, offset, chunkSize)) return false;
            
            inst.stateBlob.resize(chunkSize);
            if (!readBytes(inBuffer, offset, inst.stateBlob.data(), chunkSize)) return false;
        }

        // Insert Plugins
        uint32_t insertsCount = 0;
        if (!readT(inBuffer, offset, insertsCount)) return false;
        track.inserts.resize(insertsCount);
        for (uint32_t s = 0; s < insertsCount; ++s) {
            auto& insert = track.inserts[s];
            uint8_t bypassedByte = 0;
            uint32_t chunkSize = 0;
            
            if (!readT(inBuffer, offset, insert.first)) return false; // slotIdx
            auto& fx = insert.second;
            if (!readT(inBuffer, offset, fx.pluginId)) return false;
            if (!readT(inBuffer, offset, bypassedByte)) return false;
            fx.bypassed = (bypassedByte != 0);
            if (!readString(inBuffer, offset, fx.name)) return false;
            if (!readT(inBuffer, offset, chunkSize)) return false;
            
            fx.stateBlob.resize(chunkSize);
            if (!readBytes(inBuffer, offset, fx.stateBlob.data(), chunkSize)) return false;
        }
    }

    // ---- Markers ----
    uint32_t markerCount = 0;
    if (offset < inBuffer.size() && readT(inBuffer, offset, markerCount)) {
        outState.markers.resize(markerCount);
        for (uint32_t i = 0; i < markerCount; ++i) {
            auto& marker = outState.markers[i];
            if (!readBytes(inBuffer, offset, marker.uuid.bytes, 16)) return false;
            if (!readT(inBuffer, offset, marker.framePosition)) return false;
            if (!readBytes(inBuffer, offset, marker.label, MAX_NAME_LENGTH)) return false;
            if (!readT(inBuffer, offset, marker.colorARGB)) return false;
        }
    }

    // ---- Key Signatures ----
    uint32_t keySigCount = 0;
    if (offset < inBuffer.size() && readT(inBuffer, offset, keySigCount)) {
        outState.keySignatures.resize(keySigCount);
        for (uint32_t i = 0; i < keySigCount; ++i) {
            auto& ev = outState.keySignatures[i];
            uint8_t rootByte = 0;
            uint8_t typeByte = 0;
            if (!readT(inBuffer, offset, ev.positionSample)) return false;
            if (!readT(inBuffer, offset, rootByte)) return false;
            if (!readT(inBuffer, offset, typeByte)) return false;
            ev.rootNote = static_cast<PitchClass>(rootByte);
            ev.type = static_cast<KeyType>(typeByte);
        }
    }

    // ---- Mix Statistics ----
    uint8_t isAnalyzedByte = 0;
    if (offset < inBuffer.size() && readT(inBuffer, offset, isAnalyzedByte)) {
        outState.mixStats.isAnalyzed = (isAnalyzedByte != 0);
        if (outState.mixStats.isAnalyzed) {
            if (!readT(inBuffer, offset, outState.mixStats.integratedLoudnessLUFS)) return false;
            if (!readT(inBuffer, offset, outState.mixStats.truePeakDBTP)) return false;
            uint8_t clippingByte = 0;
            if (!readT(inBuffer, offset, clippingByte)) return false;
            outState.mixStats.clippingDetected = (clippingByte != 0);
        }
    } else {
        outState.mixStats.isAnalyzed = false;
    }

    // ---- Region Metadata ----
    uint32_t metaCount = 0;
    if (offset < inBuffer.size() && readT(inBuffer, offset, metaCount)) {
        outState.regionMetadata.resize(metaCount);
        for (uint32_t i = 0; i < metaCount; ++i) {
            auto& item = outState.regionMetadata[i];
            uint64_t rawId = 0;
            if (!readT(inBuffer, offset, rawId)) return false;
            item.regionId = RegionID::fromRaw(rawId);
            if (!readBytes(inBuffer, offset, item.metadata.name, MAX_NAME_LENGTH)) return false;
            if (!readBytes(inBuffer, offset, item.metadata.comment, MAX_COMMENT_LENGTH)) return false;
            if (!readT(inBuffer, offset, item.metadata.colorARGB)) return false;
            uint8_t hasComm = 0;
            if (!readT(inBuffer, offset, hasComm)) return false;
            item.metadata.hasComment = (hasComm != 0);
        }
    }

    return true;
}

} // namespace composition
