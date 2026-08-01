#include "midi_sequencer_node.h"
#include "common/dsp/event_scanner.h"
#include "Core infrastructure/bridges/ievent_queue.h"
#include <algorithm>
#include <cstring>

namespace DSP {

static auto& s_registry = MidiSequencerFactory::getRegistry();

void processMidiSequencer(
    NodeID nodeId,
    float* const* /*inputs*/,
    float* const* /*outputs*/,
    uint32_t /*numChannels*/,
    uint32_t numSamples,
    const EventData* events,
    uint32_t numEvents,
    EventData* outEvents,
    uint32_t* outEventCount,
    const ProcessContext* context,
    const bool* /*inputSilence*/,
    bool* /*isOutputSilent*/
) {
    auto* s = VALIDATE_STATE(s_registry, nodeId);
    if (!s || numSamples == 0 || !outEvents || !outEventCount) return;

    *outEventCount = 0;
    if (numEvents > 0 && outEvents && events) {
        uint32_t toCopy = std::min(numEvents, static_cast<uint32_t>(512));
        std::memcpy(outEvents, events, toCopy * sizeof(EventData));
        *outEventCount = toCopy;
    }

    if (!context || !context->timelineSnapshot || !context->midiClipDataProvider) {
        return;
    }

    EventScanner scanner(events, numEvents);
    const TimelineSnapshot* snapshot = context->timelineSnapshot;
    bool isPlaying = (context->transportState == TransportState::PLAYING);
    uint64_t startSample = context->transport.positionSample;

    // 1. Process sample-accurate events at this specific offset (e.g. automation)
    for (uint32_t i = 0; i < numSamples; ++i) {
        scanner.processEventsAtOffset(i, [&](const EventData& /*ev*/) {
            // Sequencer does not currently consume automation events
        });
    }

    if (!isPlaying) {
        s->hasLastExpected = false; // Reset tracking when stopped
        return;
    }

    // 2. Seek & Panic Detection
    bool seekOccurred = s->hasLastExpected && (startSample != s->lastExpectedSample);
    if (seekOccurred) {
        uint64_t lastPlayedSample = s->lastExpectedSample - 1;

        // Find notes that were active at the last played sample and send NoteOffs
        for (uint32_t r = 0; r < snapshot->regionCount; ++r) {
            const auto& region = snapshot->regions[r];
            if (region.trackId == s->trackId && region.type == RegionType::MIDI) {
                ClipID clipId = ClipID::fromRaw((static_cast<uint64_t>(1) << 32) | region.sourceId);
                
                static constexpr uint32_t TEMP_MAX = 256;
                MIDINote tempNotes[TEMP_MAX];
                uint32_t noteCount = context->midiClipDataProvider->getNotesInClip(clipId, tempNotes, TEMP_MAX);

                for (uint32_t n = 0; n < noteCount; ++n) {
                    double ratio = (region.playbackRatio > 0.0f) ? static_cast<double>(region.playbackRatio) : 1.0;
                    uint64_t scaledOffset = static_cast<uint64_t>(std::round(static_cast<double>(tempNotes[n].offsetSample) / ratio));
                    uint64_t scaledDuration = static_cast<uint64_t>(std::round(static_cast<double>(tempNotes[n].durationSample) / ratio));

                    uint64_t absStart = region.positionSample + scaledOffset;
                    uint64_t absEnd = absStart + scaledDuration;

                    if (absStart <= lastPlayedSample && absEnd > lastPlayedSample) {
                        if (*outEventCount < 512) {
                            outEvents[*outEventCount] = Layer2::EventHelpers::makeMIDINoteOff(
                                nodeId, tempNotes[n].pitch, 0, tempNotes[n].channel, 0
                            );
                            (*outEventCount)++;
                        }
                    }
                }
            }
        }

        // Send All Notes Off & All Sound Off controllers for all 16 channels to flush synth voices
        for (uint8_t ch = 0; ch < 16; ++ch) {
            if (*outEventCount < 510) {
                outEvents[*outEventCount] = Layer2::EventHelpers::makeMIDICC(nodeId, 123, 0, ch, 0);
                (*outEventCount)++;
                outEvents[*outEventCount] = Layer2::EventHelpers::makeMIDICC(nodeId, 120, 0, ch, 0);
                (*outEventCount)++;
            }
        }
    }

    s->lastExpectedSample = startSample + numSamples;
    s->hasLastExpected = true;

    // 3. Event rendering lambda
    auto renderRange = [&](uint64_t rangeStart, uint64_t rangeEnd, uint32_t blockOffset) {
        for (uint32_t r = 0; r < snapshot->regionCount; ++r) {
            const auto& region = snapshot->regions[r];
            if (region.trackId == s->trackId && region.type == RegionType::MIDI && !region.isMuted) {
                ClipID clipId = ClipID::fromRaw((static_cast<uint64_t>(1) << 32) | region.sourceId);

                static constexpr uint32_t TEMP_MAX = 256;
                MIDINote tempNotes[TEMP_MAX];
                uint32_t noteCount = context->midiClipDataProvider->getNotesInClip(clipId, tempNotes, TEMP_MAX);

                double ratio = (region.playbackRatio > 0.0f) ? static_cast<double>(region.playbackRatio) : 1.0;

                for (uint32_t n = 0; n < noteCount; ++n) {
                    uint64_t scaledOffset = static_cast<uint64_t>(std::round(static_cast<double>(tempNotes[n].offsetSample) / ratio));
                    uint64_t scaledDuration = static_cast<uint64_t>(std::round(static_cast<double>(tempNotes[n].durationSample) / ratio));

                    uint64_t absStart = region.positionSample + scaledOffset;
                    uint64_t absEnd = absStart + scaledDuration;

                    // Note On
                    if (absStart >= rangeStart && absStart < rangeEnd) {
                        uint32_t offset = blockOffset + static_cast<uint32_t>(absStart - rangeStart);
                        if (*outEventCount < 512) {
                            outEvents[*outEventCount] = Layer2::EventHelpers::makeMIDINoteOn(
                                nodeId, tempNotes[n].pitch, tempNotes[n].velocity, tempNotes[n].channel, offset
                            );
                            (*outEventCount)++;
                        }
                    }

                    // Note Off
                    if (absEnd >= rangeStart && absEnd < rangeEnd) {
                        uint32_t offset = blockOffset + static_cast<uint32_t>(absEnd - rangeStart);
                        if (*outEventCount < 512) {
                            outEvents[*outEventCount] = Layer2::EventHelpers::makeMIDINoteOff(
                                nodeId, tempNotes[n].pitch, 0, tempNotes[n].channel, offset
                            );
                            (*outEventCount)++;
                        }
                    }
                }

                // Query CC points
                MIDICCPoint tempCC[TEMP_MAX];
                uint32_t ccCount = context->midiClipDataProvider->getCCPointsInClip(clipId, tempCC, TEMP_MAX);
                for (uint32_t c = 0; c < ccCount; ++c) {
                    uint64_t scaledPos = static_cast<uint64_t>(std::round(static_cast<double>(tempCC[c].samplePosition) / ratio));
                    uint64_t absPos = region.positionSample + scaledPos;
                    if (absPos >= rangeStart && absPos < rangeEnd) {
                        uint32_t offset = blockOffset + static_cast<uint32_t>(absPos - rangeStart);
                        if (*outEventCount < 512) {
                            outEvents[*outEventCount] = Layer2::EventHelpers::makeMIDICC(
                                nodeId, tempCC[c].controllerNumber, tempCC[c].value, tempCC[c].channel, offset
                            );
                            (*outEventCount)++;
                        }
                    }
                }

                // Query Pitch points
                MIDIPitchPoint tempPitch[TEMP_MAX];
                uint32_t pbCount = context->midiClipDataProvider->getPitchPointsInClip(clipId, tempPitch, TEMP_MAX);
                for (uint32_t p = 0; p < pbCount; ++p) {
                    uint64_t scaledPos = static_cast<uint64_t>(std::round(static_cast<double>(tempPitch[p].samplePosition) / ratio));
                    uint64_t absPos = region.positionSample + scaledPos;
                    if (absPos >= rangeStart && absPos < rangeEnd) {
                        uint32_t offset = blockOffset + static_cast<uint32_t>(absPos - rangeStart);
                        if (*outEventCount < 512) {
                            outEvents[*outEventCount] = Layer2::EventHelpers::makeMIDIPitchBend(
                                nodeId, tempPitch[p].value, tempPitch[p].channel, offset
                            );
                            (*outEventCount)++;
                        }
                    }
                }
            }
        }
    };

    // 4. Render with Loop splitting or directly
    bool loopEnabled = context->loopEnabled;
    uint64_t loopStart = context->loopStart;
    uint64_t loopEnd = context->loopEnd;

    if (loopEnabled && startSample + numSamples > loopEnd && startSample < loopEnd) {
        uint32_t firstPart = static_cast<uint32_t>(loopEnd - startSample);
        uint32_t secondPart = numSamples - firstPart;
        
        // A. Render first part up to loopEnd
        renderRange(startSample, loopEnd, 0);
        
        // B. Inject loop boundary Note-Offs for crossing notes
        for (uint32_t r = 0; r < snapshot->regionCount; ++r) {
            const auto& region = snapshot->regions[r];
            if (region.trackId == s->trackId && region.type == RegionType::MIDI) {
                ClipID clipId = ClipID::fromRaw((static_cast<uint64_t>(1) << 32) | region.sourceId);
                
                static constexpr uint32_t TEMP_MAX = 256;
                MIDINote tempNotes[TEMP_MAX];
                uint32_t noteCount = context->midiClipDataProvider->getNotesInClip(clipId, tempNotes, TEMP_MAX);

                for (uint32_t n = 0; n < noteCount; ++n) {
                    double ratio = (region.playbackRatio > 0.0f) ? static_cast<double>(region.playbackRatio) : 1.0;
                    uint64_t scaledOffset = static_cast<uint64_t>(std::round(static_cast<double>(tempNotes[n].offsetSample) / ratio));
                    uint64_t scaledDuration = static_cast<uint64_t>(std::round(static_cast<double>(tempNotes[n].durationSample) / ratio));

                    uint64_t absStart = region.positionSample + scaledOffset;
                    uint64_t absEnd = absStart + scaledDuration;

                    if (absStart < loopEnd && absEnd > loopEnd && absStart >= startSample) {
                        if (*outEventCount < 512) {
                            outEvents[*outEventCount] = Layer2::EventHelpers::makeMIDINoteOff(
                                nodeId, tempNotes[n].pitch, 0, tempNotes[n].channel, firstPart - 1
                            );
                            (*outEventCount)++;
                        }
                    }
                }
            }
        }
        
        // C. Render second part starting at loopStart
        renderRange(loopStart, loopStart + secondPart, firstPart);
    } else {
        renderRange(startSample, startSample + numSamples, 0);
    }

    // 5. Sort output events chronologically
    if (*outEventCount > 1) {
        ::sort_events_in_place(outEvents, *outEventCount);
    }
}

} // namespace DSP
