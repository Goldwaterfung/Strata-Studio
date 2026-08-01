#include "project_json_serializer.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <cstdio>

using json = nlohmann::json;

namespace composition {

namespace {

// =============================================================================
// Base64 Helpers
// =============================================================================

static std::string base64_encode(const std::vector<uint8_t>& data) {
    static const char lookup[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    int val = 0, valb = -6;
    for (uint8_t c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(lookup[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        out.push_back(lookup[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (out.size() % 4) {
        out.push_back('=');
    }
    return out;
}

static std::vector<uint8_t> base64_decode(const std::string& in) {
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) {
        T[static_cast<size_t>("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i])] = i;
    }
    std::vector<uint8_t> out;
    int val = 0, valb = -8;
    for (char c : in) {
        if (T[static_cast<size_t>(c)] == -1) {
            if (c == '=') break;
            continue;
        }
        val = (val << 6) + T[static_cast<size_t>(c)];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// =============================================================================
// UUID Helpers
// =============================================================================

static std::string uuid_to_hex(const MarkerUUID& uuid) {
    char buf[33];
    for (int i = 0; i < 16; ++i) {
        std::snprintf(buf + (i * 2), 3, "%02x", uuid.bytes[i]);
    }
    buf[32] = '\0';
    return std::string(buf);
}

static MarkerUUID hex_to_uuid(const std::string& hex) {
    MarkerUUID uuid{};
    if (hex.size() == 32) {
        for (int i = 0; i < 16; ++i) {
            unsigned int byteVal = 0;
            std::sscanf(hex.c_str() + (i * 2), "%2x", &byteVal);
            uuid.bytes[i] = static_cast<uint8_t>(byteVal);
        }
    }
    return uuid;
}

// =============================================================================
// JSON conversions for Nested primitives
// =============================================================================

json serializeMusicalPosition(const MusicalPosition& pos) {
    return {
        {"bar", pos.bar},
        {"beat", pos.beat},
        {"tick", pos.tick},
        {"totalTicks", pos.totalTicks}
    };
}

MusicalPosition deserializeMusicalPosition(const json& j) {
    MusicalPosition pos{};
    pos.bar = j.value("bar", 0u);
    pos.beat = j.value("beat", static_cast<uint16_t>(0));
    pos.tick = j.value("tick", 0u);
    pos.totalTicks = j.value("totalTicks", 0u);
    return pos;
}

json serializeTimelineRegion(const TimelineRegion& reg) {
    return {
        {"type", static_cast<int>(reg.type)},
        {"sourceId", {{"id", reg.sourceId.id}, {"generation", reg.sourceId.generation}}},
        {"positionSample", reg.positionSample},
        {"sourceStartSample", reg.sourceStartSample},
        {"sourceLength", reg.sourceLength},
        {"fadeInSamples", reg.fadeInSamples},
        {"fadeOutSamples", reg.fadeOutSamples},
        {"gain", reg.gain},
        {"isMuted", reg.isMuted},
        {"startPosition", serializeMusicalPosition(reg.startPosition)},
        {"warpMode", static_cast<int>(reg.warpMode)},
        {"playbackRatio", reg.playbackRatio},
        {"sourceBpm", reg.sourceBpm}
    };
}

TimelineRegion deserializeTimelineRegion(const json& j) {
    TimelineRegion reg{};
    reg.type = static_cast<RegionType>(j.value("type", 0));
    if (j.contains("sourceId")) {
        reg.sourceId.id = j["sourceId"].value("id", 0u);
        reg.sourceId.generation = j["sourceId"].value("generation", 0u);
    }
    reg.positionSample = j.value("positionSample", 0uLL);
    reg.sourceStartSample = j.value("sourceStartSample", 0uLL);
    reg.sourceLength = j.value("sourceLength", 0uLL);
    reg.fadeInSamples = j.value("fadeInSamples", 0u);
    reg.fadeOutSamples = j.value("fadeOutSamples", 0u);
    reg.gain = j.value("gain", 1.0f);
    reg.isMuted = j.value("isMuted", false);
    if (j.contains("startPosition")) {
        reg.startPosition = deserializeMusicalPosition(j["startPosition"]);
    }
    reg.warpMode = static_cast<WarpMode>(j.value("warpMode", 0));
    reg.playbackRatio = j.value("playbackRatio", 1.0f);
    reg.sourceBpm = j.value("sourceBpm", 120.0f);
    return reg;
}

json serializeMIDINote(const MIDINote& note) {
    return {
        {"noteId", {{"id", note.noteId.id}, {"generation", note.noteId.generation}}},
        {"startPosition", serializeMusicalPosition(note.startPosition)},
        {"endPosition", serializeMusicalPosition(note.endPosition)},
        {"offsetSample", note.offsetSample},
        {"durationSample", note.durationSample},
        {"startSample", note.startSample},
        {"endSample", note.endSample},
        {"pitch", note.pitch},
        {"velocity", note.velocity},
        {"channel", note.channel}
    };
}

MIDINote deserializeMIDINote(const json& j) {
    MIDINote note{};
    if (j.contains("noteId")) {
        note.noteId.id = j["noteId"].value("id", 0u);
        note.noteId.generation = j["noteId"].value("generation", 0u);
    }
    if (j.contains("startPosition")) {
        note.startPosition = deserializeMusicalPosition(j["startPosition"]);
    }
    if (j.contains("endPosition")) {
        note.endPosition = deserializeMusicalPosition(j["endPosition"]);
    }
    note.offsetSample = j.value("offsetSample", 0uLL);
    note.durationSample = j.value("durationSample", 0uLL);
    note.startSample = j.value("startSample", 0uLL);
    note.endSample = j.value("endSample", 0uLL);
    note.pitch = j.value("pitch", static_cast<uint8_t>(0));
    note.velocity = j.value("velocity", static_cast<uint8_t>(0));
    note.channel = j.value("channel", static_cast<uint8_t>(0));
    return note;
}

} // namespace

// =============================================================================
// Serialize to JSON
// =============================================================================

bool ProjectJsonSerializer::serialize(
    const ProjectState& state,
    std::string& outJsonString
) {
    json root;
    root["format_version"] = "AGDAW_JSON_V6";

    // 1. Metadata
    root["metadata"] = {
        {"projectName", state.metadata.projectName},
        {"author", state.metadata.author},
        {"sampleRate", state.metadata.sampleRate},
        {"initialTempoBPM", state.metadata.initialTempoBPM},
        {"timeSignatureNumerator", state.metadata.timeSignatureNumerator},
        {"timeSignatureDenominator", state.metadata.timeSignatureDenominator},
        {"targetBitDepth", state.metadata.targetBitDepth},
        {"sessionDurationSeconds", state.metadata.sessionDurationSeconds}
    };

    // 2. Global Sources
    root["sources"] = json::array();
    for (const auto& src : state.sources) {
        json srcJson = {
            {"id", src.id},
            {"generation", src.generation},
            {"nameId", src.nameId},
            {"totalLengthSamples", src.totalLengthSamples},
            {"channelCount", src.channelCount},
            {"sampleRate", src.sampleRate},
            {"mediaId", src.mediaId},
            {"filePath", src.filePath},
            {"relativeFilePath", src.relativeFilePath}
        };
        root["sources"].push_back(srcJson);
    }

    // 3. Tracks
    root["tracks"] = json::array();
    for (const auto& track : state.tracks) {
        json tJson = {
            {"trackId", {{"id", track.trackId.id}, {"generation", track.trackId.generation}}},
            {"type", static_cast<int>(track.type)},
            {"name", track.name},
            {"colorARGB", track.colorARGB},
            {"audioChannelCount", track.audioChannelCount},
            {"isRecordArmed", track.isRecordArmed},
            {"isInputMonitoring", track.isInputMonitoring},
            {"comments", track.comments},
            {"outputTargetTrackId", {{"id", track.outputTargetTrackId.id}, {"generation", track.outputTargetTrackId.generation}}},
            {"inputSourceIndex", track.inputSourceIndex},
            {"automationMode", track.automationMode}
        };

        // Automation Lanes
        tJson["automationLanes"] = json::array();
        for (const auto& lane : track.automationLanes) {
            json lJson = {
                {"roleType", lane.roleType},
                {"slotIdx", lane.slotIdx},
                {"semanticNameId", lane.semanticNameId},
                {"cachedParameterIndex", lane.cachedParameterIndex},
                {"subNodeId", lane.subNodeId}
            };
            lJson["points"] = json::array();
            for (const auto& pt : lane.points) {
                lJson["points"].push_back({
                    {"positionSample", pt.positionSample},
                    {"value", pt.value},
                    {"curveShape", pt.curveShape},
                    {"tension", pt.tension}
                });
            }
            tJson["automationLanes"].push_back(lJson);
        }

        // Playlist regions
        tJson["hasPlaylist"] = track.hasPlaylist;
        if (track.hasPlaylist) {
            tJson["playlistRegions"] = json::array();
            for (const auto& reg : track.playlistRegions) {
                tJson["playlistRegions"].push_back({
                    {"regionId", {{"id", reg.regionId.id}, {"generation", reg.regionId.generation}}},
                    {"layer", reg.layer},
                    {"region", serializeTimelineRegion(reg.region)}
                });
            }
        }

        // MIDI Sequencer
        tJson["hasSequencer"] = track.hasSequencer;
        if (track.hasSequencer) {
            tJson["clipPositions"] = json::array();
            for (const auto& cp : track.clipPositions) {
                tJson["clipPositions"].push_back({
                    {"clipId", cp.clipId.id}, // Using nested id value
                    {"positionSample", cp.positionSample},
                    {"sourceLength", cp.sourceLength},
                    {"startPosition", serializeMusicalPosition(cp.startPosition)}
                });
            }

            tJson["notes"] = json::array();
            for (const auto& ne : track.notes) {
                tJson["notes"].push_back({
                    {"noteId", {{"id", ne.noteId.id}, {"generation", ne.noteId.generation}}},
                    {"clipId", ne.clipId.id},
                    {"note", serializeMIDINote(ne.note)}
                });
            }

            tJson["ccPoints"] = json::array();
            for (const auto& ce : track.ccPoints) {
                tJson["ccPoints"].push_back({
                    {"clipId", ce.clipId.id},
                    {"absoluteTickPosition", ce.point.absoluteTickPosition},
                    {"samplePosition", ce.point.samplePosition},
                    {"channel", ce.point.channel},
                    {"controllerNumber", ce.point.controllerNumber},
                    {"value", ce.point.value}
                });
            }

            tJson["pitchPoints"] = json::array();
            for (const auto& pe : track.pitchPoints) {
                tJson["pitchPoints"].push_back({
                    {"clipId", pe.clipId.id},
                    {"absoluteTickPosition", pe.point.absoluteTickPosition},
                    {"samplePosition", pe.point.samplePosition},
                    {"channel", pe.point.channel},
                    {"value", pe.point.value}
                });
            }
        }

        // Instrument Plugin
        tJson["hasInstrument"] = track.hasInstrument;
        if (track.hasInstrument) {
            tJson["instrument"] = {
                {"pluginId", track.instrument.pluginId},
                {"bypassed", track.instrument.bypassed},
                {"name", track.instrument.name},
                {"stateBlobBase64", base64_encode(track.instrument.stateBlob)}
            };
        }

        // Insert Plugins
        tJson["inserts"] = json::array();
        for (const auto& insert : track.inserts) {
            tJson["inserts"].push_back({
                {"slotIdx", insert.first},
                {"pluginId", insert.second.pluginId},
                {"bypassed", insert.second.bypassed},
                {"name", insert.second.name},
                {"stateBlobBase64", base64_encode(insert.second.stateBlob)}
            });
        }

        root["tracks"].push_back(tJson);
    }

    // 4. Markers
    root["markers"] = json::array();
    for (const auto& marker : state.markers) {
        root["markers"].push_back({
            {"uuid", uuid_to_hex(marker.uuid)},
            {"framePosition", marker.framePosition},
            {"label", std::string(marker.label)},
            {"colorARGB", marker.colorARGB}
        });
    }

    // 5. Key Signatures
    root["keySignatures"] = json::array();
    for (const auto& ev : state.keySignatures) {
        root["keySignatures"].push_back({
            {"positionSample", ev.positionSample},
            {"rootNote", static_cast<int>(ev.rootNote)},
            {"type", static_cast<int>(ev.type)}
        });
    }

    // 6. Mix Stats
    root["mixStats"] = {
        {"isAnalyzed", state.mixStats.isAnalyzed},
        {"integratedLoudnessLUFS", state.mixStats.integratedLoudnessLUFS},
        {"truePeakDBTP", state.mixStats.truePeakDBTP},
        {"clippingDetected", state.mixStats.clippingDetected}
    };

    // 7. Region Metadata
    root["regionMetadata"] = json::array();
    for (const auto& item : state.regionMetadata) {
        json metaJson = {
            {"regionId", item.regionId.toRaw()},
            {"metadata", {
                {"name", item.metadata.name},
                {"comment", item.metadata.comment},
                {"colorARGB", item.metadata.colorARGB},
                {"hasComment", item.metadata.hasComment}
            }}
        };
        root["regionMetadata"].push_back(metaJson);
    }

    outJsonString = root.dump(4);
    return true;
}

// =============================================================================
// Deserialize from JSON
// =============================================================================

bool ProjectJsonSerializer::deserialize(
    const std::string& jsonString,
    ProjectState& outState
) {
    auto root = json::parse(jsonString, nullptr, false);
    if (root.is_discarded() || root["format_version"] != "AGDAW_JSON_V6") {
        return false;
    }

    // 1. Metadata
    if (root.contains("metadata")) {
        const auto& meta = root["metadata"];
        outState.metadata.projectName = meta.value("projectName", "");
        outState.metadata.author = meta.value("author", "");
        outState.metadata.sampleRate = meta.value("sampleRate", 44100u);
        outState.metadata.initialTempoBPM = meta.value("initialTempoBPM", 120.0f);
        outState.metadata.timeSignatureNumerator = meta.value("timeSignatureNumerator", static_cast<uint8_t>(4));
        outState.metadata.timeSignatureDenominator = meta.value("timeSignatureDenominator", static_cast<uint8_t>(4));
        outState.metadata.targetBitDepth = meta.value("targetBitDepth", 24u);
        outState.metadata.sessionDurationSeconds = meta.value("sessionDurationSeconds", 300.0);
    }

    // 2. Global Sources
    if (root.contains("sources")) {
        outState.sources.clear();
        for (const auto& src : root["sources"]) {
            AudioSourceState srcState{};
            srcState.id = src.value("id", 0u);
            srcState.generation = src.value("generation", 0u);
            srcState.nameId = src.value("nameId", 0u);
            srcState.totalLengthSamples = src.value("totalLengthSamples", 0uLL);
            srcState.channelCount = src.value("channelCount", 2u);
            srcState.sampleRate = src.value("sampleRate", 44100u);
            srcState.mediaId = src.value("mediaId", 0u);
            srcState.filePath = src.value("filePath", "");
            srcState.relativeFilePath = src.value("relativeFilePath", "");
            outState.sources.push_back(srcState);
        }
    }

    // 3. Tracks
    if (root.contains("tracks")) {
        outState.tracks.clear();
        for (const auto& track : root["tracks"]) {
            TrackState tState{};
            if (track.contains("trackId")) {
                tState.trackId.id = track["trackId"].value("id", 0u);
                tState.trackId.generation = track["trackId"].value("generation", 0u);
            }
            tState.type = static_cast<TrackType>(track.value("type", 0));
            tState.name = track.value("name", "");
            tState.colorARGB = track.value("colorARGB", 0u);
            tState.audioChannelCount = track.value("audioChannelCount", 2u);
            tState.isRecordArmed = track.value("isRecordArmed", false);
            tState.isInputMonitoring = track.value("isInputMonitoring", false);
            tState.comments = track.value("comments", "");
            if (track.contains("outputTargetTrackId")) {
                tState.outputTargetTrackId.id = track["outputTargetTrackId"].value("id", 0u);
                tState.outputTargetTrackId.generation = track["outputTargetTrackId"].value("generation", 0u);
            }
            tState.inputSourceIndex = track.value("inputSourceIndex", 0u);
            tState.automationMode = track.value("automationMode", static_cast<uint8_t>(0));

            // Automation Lanes
            if (track.contains("automationLanes")) {
                for (const auto& lane : track["automationLanes"]) {
                    AutomationLaneState lState{};
                    lState.roleType = lane.value("roleType", static_cast<uint8_t>(0));
                    lState.slotIdx = lane.value("slotIdx", static_cast<uint8_t>(0));
                    lState.semanticNameId = lane.value("semanticNameId", 0u);
                    lState.cachedParameterIndex = lane.value("cachedParameterIndex", 0u);
                    lState.subNodeId = lane.value("subNodeId", 0u);

                    if (lane.contains("points")) {
                        for (const auto& pt : lane["points"]) {
                            ::AutomationPoint point{};
                            point.positionSample = pt.value("positionSample", 0uLL);
                            point.value = pt.value("value", 0.0f);
                            point.curveShape = static_cast<::AutomationPoint::Shape>(pt.value("curveShape", 0));
                            point.tension = pt.value("tension", 0.0f);
                            lState.points.push_back(point);
                        }
                    }
                    tState.automationLanes.push_back(lState);
                }
            }

            // Playlist regions
            tState.hasPlaylist = track.value("hasPlaylist", false);
            if (tState.hasPlaylist && track.contains("playlistRegions")) {
                for (const auto& reg : track["playlistRegions"]) {
                    PlaylistRegionState regState{};
                    if (reg.contains("regionId")) {
                        regState.regionId.id = reg["regionId"].value("id", 0u);
                        regState.regionId.generation = reg["regionId"].value("generation", 0u);
                    }
                    regState.layer = reg.value("layer", 0u);
                    if (reg.contains("region")) {
                        regState.region = deserializeTimelineRegion(reg["region"]);
                    }
                    tState.playlistRegions.push_back(regState);
                }
            }

            // MIDI Sequencer
            tState.hasSequencer = track.value("hasSequencer", false);
            if (tState.hasSequencer) {
                if (track.contains("clipPositions")) {
                    for (const auto& cp : track["clipPositions"]) {
                        MIDISequencerImpl::ClipPositionEntry entry{};
                        entry.clipId.id = cp.value("clipId", 0u); // Unpack clipId from outer id or integer
                        entry.positionSample = cp.value("positionSample", 0uLL);
                        entry.sourceLength = cp.value("sourceLength", 0uLL);
                        if (cp.contains("startPosition")) {
                            entry.startPosition = deserializeMusicalPosition(cp["startPosition"]);
                        }
                        tState.clipPositions.push_back(entry);
                    }
                }

                if (track.contains("notes")) {
                    for (const auto& ne : track["notes"]) {
                        MIDISequencerImpl::NoteEntry entry{};
                        if (ne.contains("noteId")) {
                            entry.noteId.id = ne["noteId"].value("id", 0u);
                            entry.noteId.generation = ne["noteId"].value("generation", 0u);
                        }
                        entry.clipId.id = ne.value("clipId", 0u);
                        if (ne.contains("note")) {
                            entry.note = deserializeMIDINote(ne["note"]);
                        }
                        tState.notes.push_back(entry);
                    }
                }

                if (track.contains("ccPoints")) {
                    for (const auto& ce : track["ccPoints"]) {
                        MIDISequencerImpl::CCEntry entry{};
                        entry.clipId.id = ce.value("clipId", 0u);
                        entry.point.absoluteTickPosition = ce.value("absoluteTickPosition", 0uLL);
                        entry.point.samplePosition = ce.value("samplePosition", 0uLL);
                        entry.point.channel = ce.value("channel", static_cast<uint8_t>(0));
                        entry.point.controllerNumber = ce.value("controllerNumber", static_cast<uint8_t>(0));
                        entry.point.value = ce.value("value", static_cast<uint8_t>(0));
                        tState.ccPoints.push_back(entry);
                    }
                }

                if (track.contains("pitchPoints")) {
                    for (const auto& pe : track["pitchPoints"]) {
                        MIDISequencerImpl::PitchEntry entry{};
                        entry.clipId.id = pe.value("clipId", 0u);
                        entry.point.absoluteTickPosition = pe.value("absoluteTickPosition", 0uLL);
                        entry.point.samplePosition = pe.value("samplePosition", 0uLL);
                        entry.point.channel = pe.value("channel", static_cast<uint8_t>(0));
                        entry.point.value = pe.value("value", static_cast<uint16_t>(0));
                        tState.pitchPoints.push_back(entry);
                    }
                }
            }

            // Instrument Plugin
            tState.hasInstrument = track.value("hasInstrument", false);
            if (tState.hasInstrument && track.contains("instrument")) {
                const auto& inst = track["instrument"];
                tState.instrument.pluginId = inst.value("pluginId", 0u);
                tState.instrument.bypassed = inst.value("bypassed", false);
                tState.instrument.name = inst.value("name", "");
                std::string base64Blob = inst.value("stateBlobBase64", "");
                tState.instrument.stateBlob = base64_decode(base64Blob);
            }

            // Insert Plugins
            if (track.contains("inserts")) {
                for (const auto& insert : track["inserts"]) {
                    uint32_t slot = insert.value("slotIdx", 0u);
                    PluginState fxState{};
                    fxState.pluginId = insert.value("pluginId", 0u);
                    fxState.bypassed = insert.value("bypassed", false);
                    fxState.name = insert.value("name", "");
                    std::string base64Blob = insert.value("stateBlobBase64", "");
                    fxState.stateBlob = base64_decode(base64Blob);
                    tState.inserts.push_back({slot, fxState});
                }
            }

            outState.tracks.push_back(tState);
        }
    }

    // 4. Markers
    if (root.contains("markers")) {
        outState.markers.clear();
        for (const auto& marker : root["markers"]) {
            MarkerInfo mInfo{};
            mInfo.uuid = hex_to_uuid(marker.value("uuid", ""));
            mInfo.framePosition = marker.value("framePosition", 0uLL);
            std::string label = marker.value("label", "");
            std::strncpy(mInfo.label, label.c_str(), sizeof(mInfo.label) - 1);
            mInfo.label[sizeof(mInfo.label) - 1] = '\0';
            mInfo.colorARGB = marker.value("colorARGB", 0u);
            outState.markers.push_back(mInfo);
        }
    }

    // 5. Key Signatures
    if (root.contains("keySignatures")) {
        outState.keySignatures.clear();
        for (const auto& ev : root["keySignatures"]) {
            KeySignaturePoint pt{};
            pt.positionSample = ev.value("positionSample", 0uLL);
            pt.rootNote = static_cast<PitchClass>(ev.value("rootNote", 0));
            pt.type = static_cast<KeyType>(ev.value("type", 0));
            outState.keySignatures.push_back(pt);
        }
    }

    // 6. Mix Stats
    if (root.contains("mixStats")) {
        const auto& stats = root["mixStats"];
        outState.mixStats.isAnalyzed = stats.value("isAnalyzed", false);
        outState.mixStats.integratedLoudnessLUFS = stats.value("integratedLoudnessLUFS", 0.0f);
        outState.mixStats.truePeakDBTP = stats.value("truePeakDBTP", 0.0f);
        outState.mixStats.clippingDetected = stats.value("clippingDetected", false);
    }

    // 7. Region Metadata
    if (root.contains("regionMetadata")) {
        outState.regionMetadata.clear();
        for (const auto& item : root["regionMetadata"]) {
            RegionMetadataState stateItem{};
            uint64_t rawId = item.value("regionId", 0uLL);
            stateItem.regionId = RegionID::fromRaw(rawId);
            
            if (item.contains("metadata")) {
                const auto& meta = item["metadata"];
                std::string nameStr = meta.value("name", "");
                std::strncpy(stateItem.metadata.name, nameStr.c_str(), MAX_NAME_LENGTH - 1);
                stateItem.metadata.name[MAX_NAME_LENGTH - 1] = '\0';
                
                std::string commentStr = meta.value("comment", "");
                std::strncpy(stateItem.metadata.comment, commentStr.c_str(), MAX_COMMENT_LENGTH - 1);
                stateItem.metadata.comment[MAX_COMMENT_LENGTH - 1] = '\0';
                
                stateItem.metadata.colorARGB = meta.value("colorARGB", 0u);
                stateItem.metadata.hasComment = meta.value("hasComment", false);
            }
            outState.regionMetadata.push_back(stateItem);
        }
    }

    return true;
}

} // namespace composition
