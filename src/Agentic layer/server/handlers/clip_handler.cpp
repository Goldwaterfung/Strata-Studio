#include "clip_handler.h"
#include "../../../Middle Bridge/timeline/iarrangement_controller.h"
#include "../../../Middle Bridge/midi/imidi_editor_controller.h"
#include "../../../Middle Bridge/timeline/itimeline_controller.h"
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace agentic {

namespace {

uint64_t parsePositionToFrames(std::string_view posStr, bridge::ITimelineController* timeline) {
    if (posStr.empty() || !timeline) return 0;
    
    std::size_t dot1 = posStr.find('.');
    if (dot1 != std::string_view::npos) {
        std::size_t dot2 = posStr.find('.', dot1 + 1);
        if (dot2 != std::string_view::npos) {
            uint32_t bar = static_cast<uint32_t>(std::stoul(std::string(posStr.substr(0, dot1))));
            uint32_t beat = static_cast<uint32_t>(std::stoul(std::string(posStr.substr(dot1 + 1, dot2 - dot1 - 1))));
            uint32_t tick = static_cast<uint32_t>(std::stoul(std::string(posStr.substr(dot2 + 1))));
            if (bar == 0) bar = 1;
            if (beat == 0) beat = 1;
            return timeline->bbtToFrame(bar, beat, tick);
        }
    }

    try {
        double val = std::stod(std::string(posStr));
        if (timeline->getSampleRate() > 0.0) {
            return static_cast<uint64_t>(val * timeline->getSampleRate());
        }
        return 0;
    } catch (...) {
        return 0;
    }
}

uint64_t parseDurationToFrames(std::string_view durStr, bridge::ITimelineController* timeline) {
    if (durStr.empty() || !timeline) return 0;
    
    std::size_t dot1 = durStr.find('.');
    if (dot1 != std::string_view::npos) {
        std::size_t dot2 = durStr.find('.', dot1 + 1);
        if (dot2 != std::string_view::npos) {
            uint32_t bar = static_cast<uint32_t>(std::stoul(std::string(durStr.substr(0, dot1))));
            uint32_t beat = static_cast<uint32_t>(std::stoul(std::string(durStr.substr(dot1 + 1, dot2 - dot1 - 1))));
            uint32_t tick = static_cast<uint32_t>(std::stoul(std::string(durStr.substr(dot2 + 1))));
            
            uint64_t endF = timeline->bbtToFrame(1 + bar, 1 + beat, tick);
            uint64_t startF = timeline->bbtToFrame(1, 1, 0);
            return (endF >= startF) ? (endF - startF) : 0;
        }
    }

    try {
        double val = std::stod(std::string(durStr));
        if (timeline->getSampleRate() > 0.0) {
            return static_cast<uint64_t>(val * timeline->getSampleRate());
        }
        return 0;
    } catch (...) {
        return 0;
    }
}

uint16_t parseGridToTicks(std::string_view gridStr, bridge::ITimelineController* timeline) {
    if (!timeline) return 0;
    uint32_t tpb = timeline->getTicksPerBeat();
    if (tpb == 0) return 0;

    if (gridStr == "1/1" || gridStr == "1/1bar") return static_cast<uint16_t>(tpb * 4);
    if (gridStr == "1/2") return static_cast<uint16_t>(tpb * 2);
    if (gridStr == "1/4") return static_cast<uint16_t>(tpb);
    if (gridStr == "1/8") return static_cast<uint16_t>(tpb / 2);
    if (gridStr == "1/16") return static_cast<uint16_t>(tpb / 4);
    if (gridStr == "1/32") return static_cast<uint16_t>(tpb / 8);
    if (gridStr == "1/64") return static_cast<uint16_t>(tpb / 16);
    if (gridStr == "1/4T") return static_cast<uint16_t>((tpb * 2) / 3);
    if (gridStr == "1/8T") return static_cast<uint16_t>(tpb / 3);
    if (gridStr == "1/16T") return static_cast<uint16_t>(tpb / 6);
    return 0;
}

std::string pitchToNoteName(uint8_t pitch) {
    static const char* kNoteNames[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    int octave = (static_cast<int>(pitch) / 12) - 1;
    int noteMod = static_cast<int>(pitch) % 12;
    return std::string(kNoteNames[noteMod]) + std::to_string(octave);
}

bool parseMusicalPitch(std::string_view str, uint8_t& outPitch, std::string& outFormattedNote, std::string& outErrorMessage) {
    if (str.empty()) {
        outErrorMessage = "Pitch argument cannot be empty.";
        return false;
    }

    std::string s(str);

    // 1. Check if string is a pure integer (or signed integer like "-5" or "+60")
    bool isInteger = true;
    size_t startIdx = 0;
    if (s[0] == '+' || s[0] == '-') {
        startIdx = 1;
    }
    if (startIdx >= s.length()) {
        isInteger = false;
    } else {
        for (size_t i = startIdx; i < s.length(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
                isInteger = false;
                break;
            }
        }
    }

    if (isInteger) {
        try {
            long val = std::stol(s);
            if (val >= 0 && val <= 127) {
                outPitch = static_cast<uint8_t>(val);
                outFormattedNote = pitchToNoteName(outPitch);
                return true;
            } else {
                outErrorMessage = "MIDI note pitch '" + s + "' is out of valid range [0..127] (C-1 to G9).";
                return false;
            }
        } catch (...) {
            outErrorMessage = "MIDI note pitch '" + s + "' integer conversion failed.";
            return false;
        }
    }

    // 2. Parse as musical note (e.g. C4, A5, Db5, F#-1, G9)
    std::string uStr;
    uStr.reserve(s.length());
    for (char c : s) {
        uStr.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }

    int baseNote = -1;
    char firstChar = uStr[0];
    switch (firstChar) {
        case 'C': baseNote = 0; break;
        case 'D': baseNote = 2; break;
        case 'E': baseNote = 4; break;
        case 'F': baseNote = 5; break;
        case 'G': baseNote = 7; break;
        case 'A': baseNote = 9; break;
        case 'B': baseNote = 11; break;
        default:
            outErrorMessage = "Invalid MIDI pitch format '" + s + "'. Expected musical note (e.g. C4, A5, Db5) or integer (0..127).";
            return false;
    }

    size_t pos = 1;
    int accidental = 0;
    if (pos < uStr.length()) {
        char c2 = uStr[pos];
        if (c2 == '#' || c2 == 'S') {
            accidental = 1;
            pos++;
        } else if (c2 == 'B') {
            accidental = -1;
            pos++;
        }
    }

    if (pos >= uStr.length()) {
        outErrorMessage = "Invalid MIDI pitch format '" + s + "'. Missing octave number (e.g. C4, A5, Db5).";
        return false;
    }

    std::string octaveStr = uStr.substr(pos);
    try {
        size_t parsedChars = 0;
        int octave = std::stoi(octaveStr, &parsedChars);
        if (parsedChars != octaveStr.length()) {
            outErrorMessage = "Invalid MIDI pitch format '" + s + "'. Extra characters after octave.";
            return false;
        }

        int calculatedPitch = (octave + 1) * 12 + baseNote + accidental;
        if (calculatedPitch >= 0 && calculatedPitch <= 127) {
            outPitch = static_cast<uint8_t>(calculatedPitch);
            outFormattedNote = pitchToNoteName(outPitch);
            return true;
        } else {
            outErrorMessage = "MIDI note pitch '" + s + "' is out of valid range [0..127] (C-1 to G9).";
            return false;
        }
    } catch (...) {
        outErrorMessage = "Invalid MIDI pitch format '" + s + "'. Invalid octave number.";
        return false;
    }
}

} // namespace

ExecutionResult ClipHandler::handleCommand(
    const ParsedArgs& args,
    bridge::IArrangementController* arrangementController,
    bridge::IMidiEditorController* midiEditorController,
    bridge::ITimelineController* timelineController
) {
    std::string_view verb = args.getVerb();
    std::string_view sub = args.getSubcommand();

    try {
        // ── MIDI Add Note Handler ───────────────────────────────────────────
        if (verb == "midi" && sub == "add-note") {
            if (!midiEditorController) {
                return ExecutionResult::Error(ErrorCode::DAW_NOT_RUNNING, "DAW_NOT_RUNNING", "MIDI editor controller interface unavailable.");
            }

            std::string_view trackStr = args.getOption("--track", "");
            std::string_view clipStr = args.getOption("--clip", "0");
            std::string_view pitchStr = args.getOption("--pitch", "");
            std::string_view velStr = args.getOption("--velocity", "");
            std::string_view chanStr = args.getOption("--channel", "0");
            std::string_view startStr = args.getOption("--start", "");
            std::string_view durStr = args.getOption("--dur", "");

            if (trackStr.empty() || pitchStr.empty() || velStr.empty() || startStr.empty() || durStr.empty()) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS",
                    "Missing required arguments for midi add-note (--track, --pitch, --velocity, --start, --dur).");
            }

            uint32_t trackId = static_cast<uint32_t>(std::stoul(std::string(trackStr)));
            uint32_t clipId = static_cast<uint32_t>(std::stoul(std::string(clipStr)));
            
            uint8_t pitch = 0;
            std::string formattedPitch;
            std::string pitchErr;
            if (!parseMusicalPitch(pitchStr, pitch, formattedPitch, pitchErr)) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", pitchErr);
            }

            uint8_t vel = static_cast<uint8_t>(std::stoul(std::string(velStr)));
            uint8_t chan = static_cast<uint8_t>(std::stoul(std::string(chanStr)));

            if (!midiEditorController->openClip(TrackID{trackId, 0}, composition::RegionID{clipId, 0})) {
                return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "ENTITY_NOT_FOUND",
                    "Target MIDI clip ID " + std::string(clipStr) + " not found on Track " + std::string(trackStr));
            }

            if (!timelineController || timelineController->getSampleRate() <= 0.0) {
                return ExecutionResult::Error(ErrorCode::DAW_NOT_RUNNING, "DAW_NOT_RUNNING", "Timeline sample rate unavailable.");
            }
            uint64_t startFrame = parsePositionToFrames(startStr, timelineController);
            uint64_t durFrames = parseDurationToFrames(durStr, timelineController);
            if (durFrames == 0) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Invalid or zero note duration.");
            }
            uint64_t endFrame = startFrame + durFrames;

            bridge::NoteID nid = midiEditorController->addNote(pitch, vel, chan, startFrame, endFrame);
            if (nid.id == 0 || nid.id == UINT32_MAX) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Failed to add MIDI note.");
            }

            return ExecutionResult::Success("MIDI_NOTE_ADDED", {
                {"track", std::string(trackStr)},
                {"clip", std::string(clipStr)},
                {"note_id", std::to_string(nid.id)},
                {"pitch", formattedPitch},
                {"pitch_number", std::to_string(pitch)},
                {"velocity", std::string(velStr)},
                {"start", std::string(startStr)},
                {"dur", std::string(durStr)}
            });
        }

        // All remaining subcommands require arrangementController
        if (!arrangementController) {
            return ExecutionResult::Error(ErrorCode::DAW_NOT_RUNNING, "DAW_NOT_RUNNING", "Arrangement controller interface unavailable.");
        }

        // ── Clip Add Audio ──────────────────────────────────────────────────
        if (sub == "add-audio") {
            std::string_view trackStr = args.getOption("--track", "");
            std::string_view path = args.getOption("--path", "");
            std::string_view start = args.getOption("--start", "");

            if (trackStr.empty() || path.empty() || start.empty()) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS",
                    "Missing required arguments for clip add-audio (--track, --path, --start).");
            }

            std::filesystem::path fsPath(path);
            if (!std::filesystem::exists(fsPath)) {
                return ExecutionResult::Error(ErrorCode::ASSET_I_O_ERROR, "ASSET_I_O_ERROR",
                    "Audio asset file path does not exist on filesystem: " + std::string(path));
            }

            uint32_t trackId = static_cast<uint32_t>(std::stoul(std::string(trackStr)));
            uint64_t startFrame = parsePositionToFrames(start, timelineController);

            composition::RegionID rid = arrangementController->importAudioClip(
                TrackID{trackId, 0}, std::string(path).c_str(), startFrame);

            if (!rid.isValid()) {
                return ExecutionResult::Error(ErrorCode::ASSET_I_O_ERROR, "ASSET_I_O_ERROR",
                    "Failed to import audio clip asset at path: " + std::string(path));
            }

            return ExecutionResult::Success("CLIP_ADDED", {
                {"track", std::string(trackStr)},
                {"clip_id", std::to_string(rid.id)},
                {"path", std::string(path)},
                {"start", std::string(start)}
            });
        }

        // ── Clip Add MIDI ───────────────────────────────────────────────────
        if (sub == "add-midi") {
            std::string_view trackStr = args.getOption("--track", "");
            std::string_view start = args.getOption("--start", "");
            std::string_view durStr = args.getOption("--dur", "");

            if (trackStr.empty() || start.empty() || durStr.empty()) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS",
                    "Missing required arguments for clip add-midi (--track, --start, --dur).");
            }

            uint32_t trackId = static_cast<uint32_t>(std::stoul(std::string(trackStr)));
            uint64_t startFrame = parsePositionToFrames(start, timelineController);
            uint64_t durFrames = parseDurationToFrames(durStr, timelineController);
            if (durFrames == 0) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Invalid or zero clip duration.");
            }

            composition::RegionID rid = arrangementController->insertMidiClip(
                TrackID{trackId, 0}, startFrame, durFrames);

            if (!rid.isValid()) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Failed to insert MIDI clip on track.");
            }

            return ExecutionResult::Success("MIDI_CLIP_ADDED", {
                {"track", std::string(trackStr)},
                {"clip_id", std::to_string(rid.id)},
                {"start", std::string(start)},
                {"dur", std::string(durStr)}
            });
        }

        // ── Clip Split ──────────────────────────────────────────────────────
        if (sub == "split") {
            std::string_view trackStr = args.getOption("--track", "");
            std::string_view clipStr = args.getOption("--clip", "");
            std::string_view atStr = args.getOption("--at", "");

            if (trackStr.empty() || clipStr.empty() || atStr.empty()) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Missing required --track, --clip, or --at argument.");
            }

            uint32_t clipId = static_cast<uint32_t>(std::stoul(std::string(clipStr)));
            bridge::VisualRegion vreg{};
            if (!arrangementController->getVisualRegion(composition::RegionID{clipId, 0}, vreg)) {
                return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "ENTITY_NOT_FOUND",
                    "Target clip ID " + std::string(clipStr) + " not found on timeline.");
            }

            uint64_t splitFrame = parsePositionToFrames(atStr, timelineController);
            arrangementController->splitRegion(composition::RegionID{clipId, 0}, splitFrame);

            return ExecutionResult::Success("CLIP_SPLIT", {
                {"track", std::string(trackStr)},
                {"clip", std::string(clipStr)},
                {"split_at", std::string(atStr)}
            });
        }

        // ── Clip Trim Silence ───────────────────────────────────────────────
        if (sub == "trim-silence") {
            std::string_view trackStr = args.getOption("--track", "");
            std::string_view clipStr = args.getOption("--clip", "");
            std::string_view thresh = args.getOption("--threshold", "");
            std::string_view fadeStr = args.getOption("--fade-ms", "");

            if (trackStr.empty() || clipStr.empty() || thresh.empty() || fadeStr.empty()) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS",
                    "Missing required arguments for clip trim-silence (--track, --clip, --threshold, --fade-ms).");
            }

            uint32_t clipId = static_cast<uint32_t>(std::stoul(std::string(clipStr)));
            bridge::VisualRegion vreg{};
            if (!arrangementController->getVisualRegion(composition::RegionID{clipId, 0}, vreg)) {
                return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "ENTITY_NOT_FOUND",
                    "Target clip ID " + std::string(clipStr) + " not found on timeline.");
            }

            if (!timelineController || timelineController->getSampleRate() <= 0.0) {
                return ExecutionResult::Error(ErrorCode::DAW_NOT_RUNNING, "DAW_NOT_RUNNING", "Timeline sample rate unavailable.");
            }
            double sampleRate = timelineController->getSampleRate();
            double fadeMs = std::stod(std::string(fadeStr));

            uint32_t fadeFrames = static_cast<uint32_t>((fadeMs / 1000.0) * sampleRate);
            arrangementController->setRegionFades(composition::RegionID{clipId, 0}, fadeFrames, fadeFrames);

            return ExecutionResult::Success("CLIP_SILENCE_TRIMMED", {
                {"track", std::string(trackStr)},
                {"clip", std::string(clipStr)},
                {"threshold_db", std::string(thresh)},
                {"fade_ms", std::string(fadeStr)}
            });
        }

        // ── Clip / MIDI Quantize ────────────────────────────────────────────
        if (sub == "quantize") {
            std::string_view trackStr = args.getOption("--track", "");
            std::string_view clipStr = args.getOption("--clip", "0");
            std::string_view gridStr = args.getOption("--grid", "");
            std::string_view strengthStr = args.getOption("--strength", "");

            if (trackStr.empty() || gridStr.empty() || strengthStr.empty()) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS",
                    "Missing required arguments for clip quantize (--track, --grid, --strength).");
            }

            uint32_t trackId = static_cast<uint32_t>(std::stoul(std::string(trackStr)));
            uint32_t clipId = static_cast<uint32_t>(std::stoul(std::string(clipStr)));

            if (clipId > 0) {
                bridge::VisualRegion vreg{};
                if (!arrangementController->getVisualRegion(composition::RegionID{clipId, 0}, vreg)) {
                    return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "ENTITY_NOT_FOUND",
                        "Target clip ID " + std::string(clipStr) + " not found on timeline.");
                }
            }

            float strength = static_cast<float>(std::stof(std::string(strengthStr)));
            uint16_t gridTicks = parseGridToTicks(gridStr, timelineController);
            if (gridTicks == 0) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Invalid or unsupported --grid parameter.");
            }

            if (midiEditorController && midiEditorController->openClip(TrackID{trackId, 0}, composition::RegionID{clipId, 0})) {
                midiEditorController->quantizeSelectedNotes(nullptr, 0, gridTicks, strength, true, 0);
                return ExecutionResult::Success("MIDI_CLIPS_QUANTIZED", {
                    {"track", std::string(trackStr)},
                    {"clip", std::string(clipStr)},
                    {"grid", std::string(gridStr)},
                    {"strength", std::string(strengthStr)}
                });
            }

            return ExecutionResult::Success("CLIPS_QUANTIZED", {
                {"track", std::string(trackStr)},
                {"clip", std::string(clipStr)},
                {"grid", std::string(gridStr)}
            });
        }

        // ── Clip Merge ──────────────────────────────────────────────────────
        if (sub == "merge") {
            std::string_view trackStr = args.getOption("--track", "");
            std::string_view clips = args.getOption("--clips", "");
            std::string_view startStr = args.getOption("--start", "");
            std::string_view endStr = args.getOption("--end", "");

            if (trackStr.empty() || (startStr.empty() && endStr.empty() && clips.empty())) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS",
                    "Missing required arguments for clip merge (--track and --start/--end or --clips).");
            }

            uint32_t trackId = static_cast<uint32_t>(std::stoul(std::string(trackStr)));
            uint64_t startFrame = parsePositionToFrames(startStr, timelineController);
            uint64_t endFrame = parsePositionToFrames(endStr, timelineController);

            if (startFrame >= endFrame) {
                // Infer bounding window from track regions
                constexpr uint32_t maxClips = 256;
                std::vector<bridge::VisualRegion> regions(maxClips);
                uint64_t arrLen = arrangementController->getArrangementLength();
                if (arrLen == 0) arrLen = UINT64_MAX;
                uint32_t count = arrangementController->getRegionsInViewport(0, arrLen, regions.data(), maxClips);

                uint64_t minStart = UINT64_MAX;
                uint64_t maxEnd = 0;
                for (uint32_t i = 0; i < count; ++i) {
                    if (regions[i].trackId.id == trackId) {
                        minStart = std::min(minStart, regions[i].startFrame);
                        maxEnd = std::max(maxEnd, regions[i].startFrame + regions[i].durationFrames);
                    }
                }
                if (minStart < maxEnd) {
                    startFrame = minStart;
                    endFrame = maxEnd;
                }
            }

            arrangementController->mergePatternClips(TrackID{trackId, 0}, startFrame, endFrame);

            return ExecutionResult::Success("CLIPS_MERGED", {
                {"track", std::string(trackStr)},
                {"clips", std::string(clips)}
            });
        }

        // ── Clip Move ───────────────────────────────────────────────────────
        if (sub == "move") {
            std::string_view trackStr = args.getOption("--track", "");
            std::string_view clipStr = args.getOption("--clip", "");
            std::string_view toPos = args.getOption("--to-pos", "");
            if (trackStr.empty() || clipStr.empty() || toPos.empty()) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Missing required --track, --clip, or --to-pos argument.");
            }

            uint32_t clipId = static_cast<uint32_t>(std::stoul(std::string(clipStr)));
            bridge::VisualRegion vreg{};
            if (!arrangementController->getVisualRegion(composition::RegionID{clipId, 0}, vreg)) {
                return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "ENTITY_NOT_FOUND",
                    "Target clip ID " + std::string(clipStr) + " not found on timeline.");
            }

            std::string_view destTrackStr = args.getOption("--to-track", trackStr);
            uint32_t destTrackId = static_cast<uint32_t>(std::stoul(std::string(destTrackStr)));
            int64_t newStartFrame = static_cast<int64_t>(parsePositionToFrames(toPos, timelineController));

            composition::RegionID rid = arrangementController->moveRegion(
                composition::RegionID{clipId, 0}, TrackID{destTrackId, 0}, newStartFrame);

            return ExecutionResult::Success("CLIP_MOVED", {
                {"track", std::string(trackStr)},
                {"clip", std::to_string(rid.id)},
                {"dest_track", std::string(destTrackStr)},
                {"to_pos", std::string(toPos)}
            });
        }

        // ── Clip Nudge ──────────────────────────────────────────────────────
        if (sub == "nudge") {
            std::string_view trackStr = args.getOption("--track", "");
            std::string_view clipStr = args.getOption("--clip", "");
            std::string_view byStr = args.getOption("--by", "");
            if (trackStr.empty() || clipStr.empty() || byStr.empty()) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS",
                    "Missing required arguments for clip nudge (--track, --clip, --by).");
            }

            uint32_t clipId = static_cast<uint32_t>(std::stoul(std::string(clipStr)));
            uint32_t trackId = static_cast<uint32_t>(std::stoul(std::string(trackStr)));

            bridge::VisualRegion vreg{};
            if (!arrangementController->getVisualRegion(composition::RegionID{clipId, 0}, vreg)) {
                return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "ENTITY_NOT_FOUND",
                    "Target clip ID " + std::string(clipStr) + " not found on timeline.");
            }

            if (!timelineController) {
                return ExecutionResult::Error(ErrorCode::DAW_NOT_RUNNING, "DAW_NOT_RUNNING", "Timeline controller unavailable.");
            }
            std::string_view cleanBy = (byStr.starts_with('+') || byStr.starts_with('-')) ? byStr.substr(1) : byStr;
            uint16_t ticks = parseGridToTicks(cleanBy, timelineController);
            if (ticks == 0) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Invalid or unsupported --by grid fraction.");
            }
            uint64_t deltaFrames = timelineController->ticksToSamples(ticks);
            int64_t newFrame = byStr.starts_with('-') ?
                static_cast<int64_t>(vreg.startFrame) - static_cast<int64_t>(deltaFrames) :
                static_cast<int64_t>(vreg.startFrame) + static_cast<int64_t>(deltaFrames);
            if (newFrame < 0) newFrame = 0;

            arrangementController->moveRegion(composition::RegionID{clipId, 0}, TrackID{trackId, 0}, newFrame);

            return ExecutionResult::Success("CLIP_NUDGED", {
                {"track", std::string(trackStr)},
                {"clip", std::string(clipStr)},
                {"by", std::string(byStr)}
            });
        }

        // ── Clip Set Gain ───────────────────────────────────────────────────
        if (sub == "set-gain") {
            std::string_view trackStr = args.getOption("--track", "");
            std::string_view clipStr = args.getOption("--clip", "");
            std::string_view dbStr = args.getOption("--db", "");
            if (trackStr.empty() || clipStr.empty() || dbStr.empty()) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Missing required --track, --clip, or --db argument.");
            }

            uint32_t clipId = static_cast<uint32_t>(std::stoul(std::string(clipStr)));
            bridge::VisualRegion vreg{};
            if (!arrangementController->getVisualRegion(composition::RegionID{clipId, 0}, vreg)) {
                return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "ENTITY_NOT_FOUND",
                    "Target clip ID " + std::string(clipStr) + " not found on timeline.");
            }

            float dbVal = static_cast<float>(std::stof(std::string(dbStr)));
            float gainLinear = std::pow(10.0f, dbVal / 20.0f);
            arrangementController->setRegionGain(composition::RegionID{clipId, 0}, gainLinear);

            return ExecutionResult::Success("CLIP_GAIN_UPDATED", {
                {"track", std::string(trackStr)},
                {"clip", std::string(clipStr)},
                {"gain_db", std::string(dbStr)}
            });
        }

        // ── Clip Set Mute ───────────────────────────────────────────────────
        if (sub == "set-mute") {
            std::string_view trackStr = args.getOption("--track", "");
            std::string_view clipStr = args.getOption("--clip", "");
            std::string_view onStr = args.getOption("--on", "");
            if (trackStr.empty() || clipStr.empty() || onStr.empty()) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS",
                    "Missing required arguments for clip set-mute (--track, --clip, --on).");
            }

            uint32_t clipId = static_cast<uint32_t>(std::stoul(std::string(clipStr)));
            bridge::VisualRegion vreg{};
            if (!arrangementController->getVisualRegion(composition::RegionID{clipId, 0}, vreg)) {
                return ExecutionResult::Error(ErrorCode::ENTITY_NOT_FOUND, "ENTITY_NOT_FOUND",
                    "Target clip ID " + std::string(clipStr) + " not found on timeline.");
            }

            bool isMuted = (onStr == "true" || onStr == "1" || args.hasFlag("--on"));
            arrangementController->setRegionMuted(composition::RegionID{clipId, 0}, isMuted);

            return ExecutionResult::Success("CLIP_MUTE_UPDATED", {
                {"track", std::string(trackStr)},
                {"clip", std::string(clipStr)},
                {"muted", isMuted ? "true" : "false"}
            });
        }

        // ── Clip List ───────────────────────────────────────────────────────
        if (sub == "list") {
            std::string_view trackStr = args.getOption("--track", "");
            if (trackStr.empty()) {
                return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Missing required --track argument.");
            }
            std::string_view barRangeStr = args.getOption("--bar", "");
            std::vector<std::map<std::string, std::string>> clipRows;

            uint64_t searchStartFrame = 0;
            uint64_t searchEndFrame = arrangementController->getArrangementLength();
            if (searchEndFrame == 0) searchEndFrame = UINT64_MAX;

            if (!barRangeStr.empty()) {
                auto dashPos = barRangeStr.find('-');
                if (dashPos != std::string_view::npos) {
                    std::string_view startPart = barRangeStr.substr(0, dashPos);
                    std::string_view endPart = barRangeStr.substr(dashPos + 1);
                    searchStartFrame = parsePositionToFrames(startPart, timelineController);
                    searchEndFrame = parsePositionToFrames(endPart, timelineController);
                }
            }

            constexpr uint32_t maxClips = 256;
            std::vector<bridge::VisualRegion> regions(maxClips);
            uint32_t count = arrangementController->getRegionsInViewport(searchStartFrame, searchEndFrame, regions.data(), maxClips);

            uint32_t filterTrack = 0;
            if (!trackStr.empty()) {
                filterTrack = static_cast<uint32_t>(std::stoul(std::string(trackStr)));
            }

            for (uint32_t i = 0; i < count; ++i) {
                const auto& r = regions[i];
                if (filterTrack != 0 && r.trackId.id != filterTrack) {
                    continue;
                }

                uint32_t bar = 1, beat = 1, tick = 0;
                if (timelineController) {
                    timelineController->frameToBBT(r.startFrame, bar, beat, tick);
                }

                std::string bbt = std::to_string(bar) + "." + std::to_string(beat) + "." + std::to_string(tick);
                float dbGain = (r.gainLinear > 0.0f) ? (20.0f * std::log10(r.gainLinear)) : -96.0f;
                std::string clipTypeStr = (r.clipType == composition::RegionType::AUDIO) ? "AUDIO" :
                                          (r.clipType == composition::RegionType::MIDI) ? "MIDI" : "AUTOMATION";

                clipRows.push_back({
                    {"CLIP_ID", std::to_string(r.id.id)},
                    {"TRACK_ID", std::to_string(r.trackId.id)},
                    {"NAME", r.name[0] != '\0' ? std::string(r.name) : "Untitled"},
                    {"TYPE", clipTypeStr},
                    {"START_BAR", bbt},
                    {"START_FRAME", std::to_string(r.startFrame)},
                    {"DURATION_FRAMES", std::to_string(r.durationFrames)},
                    {"GAIN_DB", std::to_string(dbGain)},
                    {"MUTED", r.isMuted ? "true" : "false"}
                });
            }

            return ExecutionResult::MultiSuccess("CLIP_LIST", clipRows);
        }

    } catch (const std::exception& e) {
        return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", std::string("Argument processing exception: ") + e.what());
    } catch (...) {
        return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Unknown exception during command execution.");
    }

    return ExecutionResult::Error(ErrorCode::INVALID_ARGS, "INVALID_ARGS", "Unknown clip/midi subcommand.");
}

} // namespace agentic
