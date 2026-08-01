// src/Middle Bridge/midi_editor_controller.cpp
#include "midi/midi_editor_controller.h"
#include "Core infrastructure/clock/iclock_service.h"
#include "musical_composition/playlist/iplaylist.h"
#include <algorithm>
#include <cmath>
#include <thread>
#include <chrono>

namespace bridge {

using composition::ClipID;

MidiEditorController::MidiEditorController(
    ISessionManager* sessionManager,
    Layer2::IStringRegistry* /*stringRegistry*/,
    Layer2::ITempoService* tempoService,
    Layer2::IEventQueue* eventQueue,
    Layer2::IClockService* clockService
) : sessionManager_(sessionManager),
    tempoService_(tempoService),
    eventQueue_(eventQueue),
    clockService_(clockService),
    activeTrackIdAtomic_(TrackID::invalid()),
    activeRegionIdAtomic_(RegionID::invalid()),
    activeSequencerAtomic_(nullptr) {
    if (sessionManager_) {
        sessionManager_->registerChangeListener(this);
    }
    startTimerThread();
}

MidiEditorController::~MidiEditorController() {
    stopTimerThread();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        flushActiveNotes();
    }
    if (sessionManager_) {
        sessionManager_->unregisterChangeListener(this);
    }
}

// ── Active Clip Focus ──────────────────────────────────────────────
bool MidiEditorController::openClip(TrackID trackId, RegionID regionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto* trackManager = getTrackManager();
    if (!trackManager) return false;

    composition::TrackCreateInfo info{};
    if (!trackManager->getTrackInfo(trackId, info)) return false;

    if (info.type != composition::TrackType::MIDI && info.type != composition::TrackType::INSTRUMENT) {
        return false;
    }

    auto* seq = trackManager->getMIDISequencer(trackId);
    if (!seq) return false;

    flushActiveNotes();

    activeTrackIdAtomic_.store(trackId, std::memory_order_release);
    activeRegionIdAtomic_.store(regionId, std::memory_order_release);
    activeSequencerAtomic_.store(seq, std::memory_order_release);
    return true;
}

void MidiEditorController::closeClip() {
    std::lock_guard<std::mutex> lock(mutex_);
    flushActiveNotes();
    activeTrackIdAtomic_.store(TrackID::invalid(), std::memory_order_release);
    activeRegionIdAtomic_.store(RegionID::invalid(), std::memory_order_release);
    activeSequencerAtomic_.store(nullptr, std::memory_order_release);
}

bool MidiEditorController::hasOpenClip() const {
    return activeSequencerAtomic_.load(std::memory_order_acquire) != nullptr;
}

RegionID MidiEditorController::getOpenRegionId() const {
    return activeRegionIdAtomic_.load(std::memory_order_acquire);
}

TrackID MidiEditorController::getOpenTrackId() const {
    return activeTrackIdAtomic_.load(std::memory_order_acquire);
}

// ── Note CRUD (NRT / GUI thread only) ─────────────────────────────
NoteID MidiEditorController::addNote(uint8_t pitch, uint8_t velocity, uint8_t channel,
                                    uint64_t startFrame, uint64_t endFrame) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* activeSequencer_ = activeSequencerAtomic_.load(std::memory_order_acquire);
    if (!activeSequencer_) return {0, 0};

    composition::TimelineRegion region{};
    if (!getActiveRegion(region)) return {0, 0};
    ClipID clipId = ClipID::fromRaw(region.sourceId.toRaw());

    composition::MIDINote note{};
    note.noteId = {0, 0};
    note.pitch = pitch;
    note.velocity = velocity;
    note.channel = channel;
    note.offsetSample = (startFrame >= region.positionSample) ? (startFrame - region.positionSample) : 0;
    note.durationSample = (endFrame >= startFrame) ? (endFrame - startFrame) : 0;
    note.startSample = startFrame;
    note.endSample = endFrame;

    if (tempoService_) {
        auto bbtStart = tempoService_->samplesToBBT(startFrame);
        note.startPosition.bar = bbtStart.bar;
        note.startPosition.beat = static_cast<uint16_t>(bbtStart.beat);
        note.startPosition.tick = bbtStart.tick;

        auto bbtEnd = tempoService_->samplesToBBT(endFrame);
        note.endPosition.bar = bbtEnd.bar;
        note.endPosition.beat = static_cast<uint16_t>(bbtEnd.beat);
        note.endPosition.tick = bbtEnd.tick;
    }

    return activeSequencer_->addNote(clipId, note);
}

void MidiEditorController::removeNote(NoteID id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* activeSequencer_ = activeSequencerAtomic_.load(std::memory_order_acquire);
    if (!activeSequencer_) return;
    activeSequencer_->removeNote(id);
}

void MidiEditorController::moveNote(NoteID id, uint8_t newPitch, uint64_t newStartFrame) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* activeSequencer_ = activeSequencerAtomic_.load(std::memory_order_acquire);
    if (!activeSequencer_) return;

    composition::TimelineRegion region{};
    if (!getActiveRegion(region)) return;
    ClipID clipId = ClipID::fromRaw(region.sourceId.toRaw());

    std::vector<composition::MIDINote> notes(MAX_MIDI_NOTES_VIEWPORT);
    uint32_t count = activeSequencer_->getNotesInClip(clipId, notes.data(), static_cast<uint32_t>(notes.size()));
    if (count > notes.size()) {
        notes.resize(count);
        count = activeSequencer_->getNotesInClip(clipId, notes.data(), static_cast<uint32_t>(notes.size()));
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (notes[i].noteId == id) {
            composition::MIDINote updated = notes[i];
            updated.pitch = newPitch;
            updated.offsetSample = (newStartFrame >= region.positionSample) ? (newStartFrame - region.positionSample) : 0;
            
            uint64_t absoluteStart = region.positionSample + updated.offsetSample;
            uint64_t absoluteEnd = absoluteStart + updated.durationSample;
            updated.startSample = absoluteStart;
            updated.endSample = absoluteEnd;

            if (tempoService_) {
                auto bbtStart = tempoService_->samplesToBBT(absoluteStart);
                updated.startPosition.bar = bbtStart.bar;
                updated.startPosition.beat = static_cast<uint16_t>(bbtStart.beat);
                updated.startPosition.tick = bbtStart.tick;

                auto bbtEnd = tempoService_->samplesToBBT(absoluteEnd);
                updated.endPosition.bar = bbtEnd.bar;
                updated.endPosition.beat = static_cast<uint16_t>(bbtEnd.beat);
                updated.endPosition.tick = bbtEnd.tick;
            }

            activeSequencer_->updateNote(id, updated);
            break;
        }
    }
}

void MidiEditorController::resizeNote(NoteID id, uint64_t newEndFrame) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* activeSequencer_ = activeSequencerAtomic_.load(std::memory_order_acquire);
    if (!activeSequencer_) return;

    composition::TimelineRegion region{};
    if (!getActiveRegion(region)) return;
    ClipID clipId = ClipID::fromRaw(region.sourceId.toRaw());

    std::vector<composition::MIDINote> notes(MAX_MIDI_NOTES_VIEWPORT);
    uint32_t count = activeSequencer_->getNotesInClip(clipId, notes.data(), static_cast<uint32_t>(notes.size()));
    if (count > notes.size()) {
        notes.resize(count);
        count = activeSequencer_->getNotesInClip(clipId, notes.data(), static_cast<uint32_t>(notes.size()));
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (notes[i].noteId == id) {
            composition::MIDINote updated = notes[i];
            uint64_t absoluteStart = region.positionSample + updated.offsetSample;
            if (newEndFrame > absoluteStart) {
                updated.durationSample = newEndFrame - absoluteStart;
                updated.startSample = absoluteStart;
                updated.endSample = newEndFrame;

                if (tempoService_) {
                    auto bbtEnd = tempoService_->samplesToBBT(newEndFrame);
                    updated.endPosition.bar = bbtEnd.bar;
                    updated.endPosition.beat = static_cast<uint16_t>(bbtEnd.beat);
                    updated.endPosition.tick = bbtEnd.tick;
                }

                activeSequencer_->updateNote(id, updated);
            }
            break;
        }
    }
}

void MidiEditorController::setNoteVelocity(NoteID id, uint8_t velocity) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* activeSequencer_ = activeSequencerAtomic_.load(std::memory_order_acquire);
    if (!activeSequencer_) return;

    composition::TimelineRegion region{};
    if (!getActiveRegion(region)) return;
    ClipID clipId = ClipID::fromRaw(region.sourceId.toRaw());

    std::vector<composition::MIDINote> notes(MAX_MIDI_NOTES_VIEWPORT);
    uint32_t count = activeSequencer_->getNotesInClip(clipId, notes.data(), static_cast<uint32_t>(notes.size()));
    if (count > notes.size()) {
        notes.resize(count);
        count = activeSequencer_->getNotesInClip(clipId, notes.data(), static_cast<uint32_t>(notes.size()));
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (notes[i].noteId == id) {
            composition::MIDINote updated = notes[i];
            updated.velocity = velocity;
            activeSequencer_->updateNote(id, updated);
            break;
        }
    }
}

// ── Batch / Selection Operations ──────────────────────────────────
void MidiEditorController::removeSelectedNotes(const NoteID* ids, uint32_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* activeSequencer_ = activeSequencerAtomic_.load(std::memory_order_acquire);
    if (!activeSequencer_ || !ids || count == 0) return;

    auto* session = sessionManager_->getActiveSession();
    auto* history = session ? session->getCommandHistory() : nullptr;
    if (history) history->beginCompound();

    for (uint32_t i = 0; i < count; ++i) {
        activeSequencer_->removeNote(ids[i]);
    }

    if (history) history->endCompound();
}

void MidiEditorController::transposeSelectedNotes(const NoteID* ids, uint32_t count, int8_t semitones) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* activeSequencer_ = activeSequencerAtomic_.load(std::memory_order_acquire);
    if (!activeSequencer_ || !ids || count == 0) return;

    composition::TimelineRegion region{};
    if (!getActiveRegion(region)) return;
    ClipID clipId = ClipID::fromRaw(region.sourceId.toRaw());

    std::vector<composition::MIDINote> notes(MAX_MIDI_NOTES_VIEWPORT);
    uint32_t noteCount = activeSequencer_->getNotesInClip(clipId, notes.data(), static_cast<uint32_t>(notes.size()));
    if (noteCount > notes.size()) {
        notes.resize(noteCount);
        noteCount = activeSequencer_->getNotesInClip(clipId, notes.data(), static_cast<uint32_t>(notes.size()));
    }

    auto* session = sessionManager_->getActiveSession();
    auto* history = session ? session->getCommandHistory() : nullptr;
    if (history) history->beginCompound();

    for (uint32_t i = 0; i < count; ++i) {
        auto id = ids[i];
        for (uint32_t j = 0; j < noteCount; ++j) {
            if (notes[j].noteId == id) {
                composition::MIDINote updated = notes[j];
                int32_t newPitch = static_cast<int32_t>(updated.pitch) + semitones;
                updated.pitch = static_cast<uint8_t>(std::clamp(newPitch, 0, 127));
                activeSequencer_->updateNote(id, updated);
                break;
            }
        }
    }

    if (history) history->endCompound();
}

void MidiEditorController::shiftSelectedNotes(const NoteID* ids, uint32_t count, int64_t deltaFrames) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* activeSequencer_ = activeSequencerAtomic_.load(std::memory_order_acquire);
    if (!activeSequencer_ || !ids || count == 0) return;

    composition::TimelineRegion region{};
    if (!getActiveRegion(region)) return;
    ClipID clipId = ClipID::fromRaw(region.sourceId.toRaw());

    std::vector<composition::MIDINote> notes(MAX_MIDI_NOTES_VIEWPORT);
    uint32_t noteCount = activeSequencer_->getNotesInClip(clipId, notes.data(), static_cast<uint32_t>(notes.size()));
    if (noteCount > notes.size()) {
        notes.resize(noteCount);
        noteCount = activeSequencer_->getNotesInClip(clipId, notes.data(), static_cast<uint32_t>(notes.size()));
    }

    auto* session = sessionManager_->getActiveSession();
    auto* history = session ? session->getCommandHistory() : nullptr;
    if (history) history->beginCompound();

    for (uint32_t i = 0; i < count; ++i) {
        auto id = ids[i];
        for (uint32_t j = 0; j < noteCount; ++j) {
            if (notes[j].noteId == id) {
                composition::MIDINote updated = notes[j];
                if (deltaFrames >= 0) {
                    updated.offsetSample += static_cast<uint64_t>(deltaFrames);
                } else {
                    uint64_t absoluteDelta = static_cast<uint64_t>(-deltaFrames);
                    if (updated.offsetSample >= absoluteDelta) {
                        updated.offsetSample -= absoluteDelta;
                    } else {
                        updated.offsetSample = 0;
                    }
                }

                uint64_t absoluteStart = region.positionSample + updated.offsetSample;
                uint64_t absoluteEnd = absoluteStart + updated.durationSample;
                updated.startSample = absoluteStart;
                updated.endSample = absoluteEnd;

                if (tempoService_) {
                    auto bbtStart = tempoService_->samplesToBBT(absoluteStart);
                    updated.startPosition.bar = bbtStart.bar;
                    updated.startPosition.beat = static_cast<uint16_t>(bbtStart.beat);
                    updated.startPosition.tick = bbtStart.tick;

                    auto bbtEnd = tempoService_->samplesToBBT(absoluteEnd);
                    updated.endPosition.bar = bbtEnd.bar;
                    updated.endPosition.beat = static_cast<uint16_t>(bbtEnd.beat);
                    updated.endPosition.tick = bbtEnd.tick;
                }

                activeSequencer_->updateNote(id, updated);
                break;
            }
        }
    }

    if (history) history->endCompound();
}

// ── Quantization ──────────────────────────────────────────────────
void MidiEditorController::quantizeSelectedNotes(const NoteID* ids, uint32_t count,
                                                uint16_t gridResolutionTicks,
                                                float strength,
                                                bool  quantizeEnds,
                                                int   swingPercentage) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* activeSequencer_ = activeSequencerAtomic_.load(std::memory_order_acquire);
    if (!activeSequencer_ || !ids || count == 0 || !tempoService_) return;

    composition::TimelineRegion region{};
    if (!getActiveRegion(region)) return;
    ClipID clipId = ClipID::fromRaw(region.sourceId.toRaw());

    std::vector<composition::MIDINote> notes(MAX_MIDI_NOTES_VIEWPORT);
    uint32_t noteCount = activeSequencer_->getNotesInClip(clipId, notes.data(), static_cast<uint32_t>(notes.size()));
    if (noteCount > notes.size()) {
        notes.resize(noteCount);
        noteCount = activeSequencer_->getNotesInClip(clipId, notes.data(), static_cast<uint32_t>(notes.size()));
    }

    auto ticksPerBeat = tempoService_->getTicksPerBeat();
    if (ticksPerBeat == 0) return;

    auto samplesToTicks = [&](uint64_t samples) -> uint64_t {
        double beats = tempoService_->samplesToBeats(samples);
        return static_cast<uint64_t>(std::round(beats * ticksPerBeat));
    };

    auto ticksToSamples = [&](uint64_t ticks) -> uint64_t {
        double beats = static_cast<double>(ticks) / ticksPerBeat;
        return tempoService_->beatsToSamples(beats);
    };

    auto roundToGrid = [&](uint64_t absoluteTicks) -> uint64_t {
        if (gridResolutionTicks == 0) return absoluteTicks;
        uint64_t evenOddInterval = gridResolutionTicks * 2;
        uint64_t pairIndex = absoluteTicks / evenOddInterval;
        uint64_t p0 = pairIndex * evenOddInterval;

        double swingFactor = (static_cast<double>(swingPercentage) - 50.0) / 100.0;
        int64_t swingTicks = static_cast<int64_t>(gridResolutionTicks * swingFactor * (2.0 / 3.0));

        uint64_t p1 = p0 + gridResolutionTicks;
        if (swingTicks >= 0) {
            p1 += static_cast<uint64_t>(swingTicks);
        } else {
            p1 -= static_cast<uint64_t>(-swingTicks);
        }
        uint64_t p2 = p0 + evenOddInterval;

        uint64_t d0 = (absoluteTicks >= p0) ? (absoluteTicks - p0) : (p0 - absoluteTicks);
        uint64_t d1 = (absoluteTicks >= p1) ? (absoluteTicks - p1) : (p1 - absoluteTicks);
        uint64_t d2 = (absoluteTicks >= p2) ? (absoluteTicks - p2) : (p2 - absoluteTicks);

        if (d0 <= d1 && d0 <= d2) return p0;
        if (d1 <= d0 && d1 <= d2) return p1;
        return p2;
    };

    auto* session = sessionManager_->getActiveSession();
    auto* history = session ? session->getCommandHistory() : nullptr;
    if (history) history->beginCompound();

    for (uint32_t i = 0; i < count; ++i) {
        auto id = ids[i];
        for (uint32_t j = 0; j < noteCount; ++j) {
            if (notes[j].noteId == id) {
                composition::MIDINote updated = notes[j];

                uint64_t absoluteStart = region.positionSample + updated.offsetSample;
                uint64_t absoluteEnd = absoluteStart + updated.durationSample;

                // 1. Quantize start
                uint64_t startTicks = samplesToTicks(absoluteStart);
                uint64_t targetStartTicks = roundToGrid(startTicks);
                int64_t diffStart = static_cast<int64_t>(targetStartTicks) - static_cast<int64_t>(startTicks);
                uint64_t quantizedStartTicks = static_cast<uint64_t>(static_cast<int64_t>(startTicks) + std::round(diffStart * strength));
                
                uint64_t oldDuration = updated.durationSample;
                uint64_t newAbsoluteStart = ticksToSamples(quantizedStartTicks);
                
                if (newAbsoluteStart >= region.positionSample) {
                    updated.offsetSample = newAbsoluteStart - region.positionSample;
                } else {
                    updated.offsetSample = 0;
                }

                // 2. Quantize end
                if (quantizeEnds) {
                    uint64_t endTicks = samplesToTicks(absoluteEnd);
                    uint64_t targetEndTicks = roundToGrid(endTicks);
                    int64_t diffEnd = static_cast<int64_t>(targetEndTicks) - static_cast<int64_t>(endTicks);
                    uint64_t quantizedEndTicks = static_cast<uint64_t>(static_cast<int64_t>(endTicks) + std::round(diffEnd * strength));
                    uint64_t newAbsoluteEnd = ticksToSamples(quantizedEndTicks);
                    if (newAbsoluteEnd > newAbsoluteStart) {
                        updated.durationSample = newAbsoluteEnd - newAbsoluteStart;
                    } else {
                        updated.durationSample = 0;
                    }
                } else {
                    updated.durationSample = oldDuration;
                }

                uint64_t finalAbsoluteStart = region.positionSample + updated.offsetSample;
                uint64_t finalAbsoluteEnd = finalAbsoluteStart + updated.durationSample;
                updated.startSample = finalAbsoluteStart;
                updated.endSample = finalAbsoluteEnd;

                auto bbtStart = tempoService_->samplesToBBT(finalAbsoluteStart);
                updated.startPosition.bar = bbtStart.bar;
                updated.startPosition.beat = static_cast<uint16_t>(bbtStart.beat);
                updated.startPosition.tick = bbtStart.tick;

                auto bbtEnd = tempoService_->samplesToBBT(finalAbsoluteEnd);
                updated.endPosition.bar = bbtEnd.bar;
                updated.endPosition.beat = static_cast<uint16_t>(bbtEnd.beat);
                updated.endPosition.tick = bbtEnd.tick;

                activeSequencer_->updateNote(id, updated);
                break;
            }
        }
    }

    if (history) history->endCompound();
}

// ── Viewport Queries (stack-allocated, zero heap) ──────────────────
uint32_t MidiEditorController::getNotesInViewport(
    uint64_t startFrame, uint64_t endFrame,
    composition::MIDINote* outNotes, uint32_t maxNotes) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* activeSequencer_ = activeSequencerAtomic_.load(std::memory_order_acquire);
    if (!activeSequencer_) return 0;

    composition::TimelineRegion region{};
    if (!getActiveRegion(region)) return 0;
    ClipID clipId = ClipID::fromRaw(region.sourceId.toRaw());

    std::vector<composition::MIDINote> notes(MAX_MIDI_NOTES_VIEWPORT);
    uint32_t count = activeSequencer_->getNotesInClip(clipId, notes.data(), static_cast<uint32_t>(notes.size()));
    if (count > notes.size()) {
        notes.resize(count);
        count = activeSequencer_->getNotesInClip(clipId, notes.data(), static_cast<uint32_t>(notes.size()));
    }

    uint32_t written = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint64_t noteStart = region.positionSample + notes[i].offsetSample;
        uint64_t noteEnd = noteStart + notes[i].durationSample;

        if (noteStart < endFrame && noteEnd > startFrame) {
            if (written < maxNotes) {
                outNotes[written] = notes[i];
                outNotes[written].startSample = noteStart;
                outNotes[written].endSample = noteEnd;
                written++;
            } else {
                break;
            }
        }
    }
    return written;
}

uint32_t MidiEditorController::getCCPointsInViewport(
    uint64_t startFrame, uint64_t endFrame,
    uint8_t controllerNumber,
    VisualCCPoint* outPoints, uint32_t maxPoints) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* activeSequencer_ = activeSequencerAtomic_.load(std::memory_order_acquire);
    if (!activeSequencer_) return 0;

    composition::TimelineRegion region{};
    if (!getActiveRegion(region)) return 0;
    ClipID clipId = ClipID::fromRaw(region.sourceId.toRaw());

    static constexpr uint32_t MAX_CC_POINTS = 4096;
    std::vector<composition::MIDICCPoint> ccPoints(MAX_CC_POINTS);
    uint32_t count = activeSequencer_->getCCPointsInClip(clipId, ccPoints.data(), MAX_CC_POINTS);
    if (count > ccPoints.size()) {
        ccPoints.resize(count);
        count = activeSequencer_->getCCPointsInClip(clipId, ccPoints.data(), count);
    }

    uint32_t written = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (ccPoints[i].controllerNumber == controllerNumber) {
            uint64_t absPos = region.positionSample + ccPoints[i].samplePosition;
            if (absPos >= startFrame && absPos < endFrame) {
                if (written < maxPoints) {
                    outPoints[written].framePosition = absPos;
                    outPoints[written].controllerNumber = ccPoints[i].controllerNumber;
                    outPoints[written].value = ccPoints[i].value;
                    written++;
                } else {
                    break;
                }
            }
        }
    }
    return written;
}

// ── CC / Expression Editing ────────────────────────────────────────
void MidiEditorController::addCCPoint(uint8_t controllerNumber, uint8_t value,
                                    uint8_t channel, uint64_t framePosition) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* activeSequencer_ = activeSequencerAtomic_.load(std::memory_order_acquire);
    if (!activeSequencer_) return;

    composition::TimelineRegion region{};
    if (!getActiveRegion(region)) return;
    ClipID clipId = ClipID::fromRaw(region.sourceId.toRaw());

    composition::MIDICCPoint pt{};
    if (tempoService_) {
        uint32_t ticksPerBeat = tempoService_->getTicksPerBeat();
        double beats = tempoService_->samplesToBeats(framePosition);
        pt.absoluteTickPosition = static_cast<uint64_t>(std::round(beats * ticksPerBeat));
    } else {
        pt.absoluteTickPosition = 0;
    }
    pt.samplePosition = (framePosition >= region.positionSample) ? (framePosition - region.positionSample) : 0;
    pt.channel = channel;
    pt.controllerNumber = controllerNumber;
    pt.value = value;

    activeSequencer_->addCCPoint(clipId, pt);
}

void MidiEditorController::removeCCPointsInRange(uint8_t controllerNumber,
                                               uint64_t startFrame, uint64_t endFrame) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* activeSequencer_ = activeSequencerAtomic_.load(std::memory_order_acquire);
    if (!activeSequencer_) return;

    composition::TimelineRegion region{};
    if (!getActiveRegion(region)) return;
    ClipID clipId = ClipID::fromRaw(region.sourceId.toRaw());

    static constexpr uint32_t MAX_CC_POINTS = 4096;
    std::vector<composition::MIDICCPoint> ccPoints(MAX_CC_POINTS);
    uint32_t count = activeSequencer_->getCCPointsInClip(clipId, ccPoints.data(), MAX_CC_POINTS);
    if (count > ccPoints.size()) {
        ccPoints.resize(count);
        count = activeSequencer_->getCCPointsInClip(clipId, ccPoints.data(), count);
    }

    std::vector<composition::MIDICCPoint> keepPoints;
    keepPoints.reserve(count);

    uint64_t relativeStart = (startFrame >= region.positionSample) ? (startFrame - region.positionSample) : 0;
    uint64_t relativeEnd = (endFrame >= region.positionSample) ? (endFrame - region.positionSample) : 0;

    for (uint32_t i = 0; i < count; ++i) {
        bool inRange = (ccPoints[i].controllerNumber == controllerNumber) &&
                       (ccPoints[i].samplePosition >= relativeStart) &&
                       (ccPoints[i].samplePosition < relativeEnd);
        if (!inRange) {
            keepPoints.push_back(ccPoints[i]);
        }
    }

    activeSequencer_->removeCCPointsInClip(clipId);

    for (const auto& pt : keepPoints) {
        activeSequencer_->addCCPoint(clipId, pt);
    }
}

// ── MIDI Note Preview ──────────────────────────────────────────────
void MidiEditorController::previewNote(uint8_t pitch, uint8_t velocity,
                                     uint8_t channel, uint32_t durationMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    TrackID activeTrack = activeTrackIdAtomic_.load(std::memory_order_acquire);
    if (!eventQueue_ || !activeTrack.isValid()) return;

    auto* trackManager = getTrackManager();
    if (!trackManager) return;

    composition::TrackPipelineDescriptor desc = trackManager->getPipelineDescriptor(activeTrack);
    NodeID targetNode = desc.instrumentSlotNode.isValid() ? desc.instrumentSlotNode : desc.sourceNode;
    
    if (targetNode.isValid()) {
        uint32_t offset = 0;
        if (clockService_) {
            uint64_t cycleStartSteadyTime = clockService_->getCycleStartSteadyTime();
            uint32_t currentNumFrames = clockService_->getCurrentNumFrames();
            
            uint64_t nowSteadyTime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count());
            
            if (nowSteadyTime > cycleStartSteadyTime) {
                uint64_t elapsedNs = nowSteadyTime - cycleStartSteadyTime;
                double sampleRate = clockService_->getSampleRate();
                double nsPerSample = 1e9 / sampleRate;
                offset = static_cast<uint32_t>(elapsedNs / nsPerSample);
                if (offset >= currentNumFrames) {
                    offset = currentNumFrames - 1;
                }
            }
        }

        // Send NOTE_ON immediately
        eventQueue_->pushEvent(Layer2::EventHelpers::makeMIDINoteOn(targetNode, pitch, velocity, channel, offset));

        // Track note as active
        ActiveNoteKey noteKey{targetNode, channel, pitch};
        if (std::find(activeNotes_.begin(), activeNotes_.end(), noteKey) == activeNotes_.end()) {
            activeNotes_.push_back(noteKey);
        }

        // Schedule NOTE_OFF using a lightweight, non-blocking persistent background timer
        scheduleTask(durationMs, [this, targetNode, pitch, channel]() {
            std::lock_guard<std::mutex> innerLock(mutex_);
            
            // Remove from active notes tracking
            ActiveNoteKey key{targetNode, channel, pitch};
            auto it = std::find(activeNotes_.begin(), activeNotes_.end(), key);
            if (it != activeNotes_.end()) {
                activeNotes_.erase(it);
            }

            uint32_t offOffset = 0;
            if (clockService_) {
                uint64_t cycleStartSteadyTime = clockService_->getCycleStartSteadyTime();
                uint32_t currentNumFrames = clockService_->getCurrentNumFrames();
                
                uint64_t nowSteadyTime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()
                ).count());
                
                if (nowSteadyTime > cycleStartSteadyTime) {
                    uint64_t elapsedNs = nowSteadyTime - cycleStartSteadyTime;
                    double sampleRate = clockService_->getSampleRate();
                    double nsPerSample = 1e9 / sampleRate;
                    offOffset = static_cast<uint32_t>(elapsedNs / nsPerSample);
                    if (offOffset >= currentNumFrames) {
                        offOffset = currentNumFrames - 1;
                    }
                }
            }
            if (eventQueue_) {
                eventQueue_->pushEvent(Layer2::EventHelpers::makeMIDINoteOff(targetNode, pitch, 0, channel, offOffset));
            }
        });
    }
}

void MidiEditorController::noteOn(uint8_t pitch, uint8_t velocity, uint8_t channel) {
    std::lock_guard<std::mutex> lock(mutex_);
    TrackID activeTrack = activeTrackIdAtomic_.load(std::memory_order_acquire);
    if (!eventQueue_ || !activeTrack.isValid()) return;

    auto* trackManager = getTrackManager();
    if (!trackManager) return;


    composition::TrackPipelineDescriptor desc = trackManager->getPipelineDescriptor(activeTrack);
    NodeID targetNode = desc.instrumentSlotNode.isValid() ? desc.instrumentSlotNode : desc.sourceNode;
    
    if (targetNode.isValid()) {
        uint32_t offset = 0;
        if (clockService_) {
            uint64_t cycleStartSteadyTime = clockService_->getCycleStartSteadyTime();
            uint32_t currentNumFrames = clockService_->getCurrentNumFrames();
            
            uint64_t nowSteadyTime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count());
            
            if (nowSteadyTime > cycleStartSteadyTime) {
                uint64_t elapsedNs = nowSteadyTime - cycleStartSteadyTime;
                double sampleRate = clockService_->getSampleRate();
                double nsPerSample = 1e9 / sampleRate;
                offset = static_cast<uint32_t>(elapsedNs / nsPerSample);
                if (offset >= currentNumFrames) {
                    offset = currentNumFrames - 1;
                }
            }
        }

        eventQueue_->pushEvent(Layer2::EventHelpers::makeMIDINoteOn(targetNode, pitch, velocity, channel, offset));

        ActiveNoteKey noteKey{targetNode, channel, pitch};
        if (std::find(activeNotes_.begin(), activeNotes_.end(), noteKey) == activeNotes_.end()) {
            activeNotes_.push_back(noteKey);
        }
    }
}

void MidiEditorController::noteOff(uint8_t pitch, uint8_t channel) {
    std::lock_guard<std::mutex> lock(mutex_);
    TrackID activeTrack = activeTrackIdAtomic_.load(std::memory_order_acquire);
    if (!eventQueue_ || !activeTrack.isValid()) return;

    auto* trackManager = getTrackManager();
    if (!trackManager) return;


    composition::TrackPipelineDescriptor desc = trackManager->getPipelineDescriptor(activeTrack);
    NodeID targetNode = desc.instrumentSlotNode.isValid() ? desc.instrumentSlotNode : desc.sourceNode;
    
    if (targetNode.isValid()) {
        uint32_t offset = 0;
        if (clockService_) {
            uint64_t cycleStartSteadyTime = clockService_->getCycleStartSteadyTime();
            uint32_t currentNumFrames = clockService_->getCurrentNumFrames();
            
            uint64_t nowSteadyTime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count());
            
            if (nowSteadyTime > cycleStartSteadyTime) {
                uint64_t elapsedNs = nowSteadyTime - cycleStartSteadyTime;
                double sampleRate = clockService_->getSampleRate();
                double nsPerSample = 1e9 / sampleRate;
                offset = static_cast<uint32_t>(elapsedNs / nsPerSample);
                if (offset >= currentNumFrames) {
                    offset = currentNumFrames - 1;
                }
            }
        }

        eventQueue_->pushEvent(Layer2::EventHelpers::makeMIDINoteOff(targetNode, pitch, 0, channel, offset));

        ActiveNoteKey noteKey{targetNode, channel, pitch};
        auto it = std::find(activeNotes_.begin(), activeNotes_.end(), noteKey);
        if (it != activeNotes_.end()) {
            activeNotes_.erase(it);
        }
    }
}

// ── Clip Boundary Editing ──────────────────────────────────────────
void MidiEditorController::setClipStart(RegionID id, uint64_t newStartFrame) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto activeTrackId_ = activeTrackIdAtomic_.load(std::memory_order_acquire);
    auto* trackManager = getTrackManager();
    if (!trackManager || !activeTrackId_.isValid()) return;
    auto* playlist = trackManager->getPlaylist(activeTrackId_);
    if (!playlist) return;

    composition::TimelineRegion region{};
    if (getActiveRegion(region)) {
        uint64_t delta = (newStartFrame > region.positionSample) ? (newStartFrame - region.positionSample) : 0;
        uint64_t newLength = (region.sourceLength > delta) ? (region.sourceLength - delta) : 0;
        playlist->trimRegion(id, newStartFrame, region.sourceStartSample + delta, newLength);

        if (region.type == composition::RegionType::MIDI) {
            if (auto* seq = activeSequencerAtomic_.load(std::memory_order_acquire)) {
                ClipID clipId = ClipID::fromRaw(region.sourceId.toRaw());
                seq->updateClipPosition(clipId, newStartFrame, newLength);
            }
        }
    }
}

void MidiEditorController::setClipEnd(RegionID id, uint64_t newEndFrame) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto activeTrackId_ = activeTrackIdAtomic_.load(std::memory_order_acquire);
    auto* trackManager = getTrackManager();
    if (!trackManager || !activeTrackId_.isValid()) return;
    auto* playlist = trackManager->getPlaylist(activeTrackId_);
    if (!playlist) return;

    composition::TimelineRegion region{};
    if (getActiveRegion(region)) {
        if (newEndFrame > region.positionSample) {
            uint64_t newLength = newEndFrame - region.positionSample;
            playlist->trimRegion(id, region.positionSample, region.sourceStartSample, newLength);

            if (region.type == composition::RegionType::MIDI) {
                if (auto* seq = activeSequencerAtomic_.load(std::memory_order_acquire)) {
                    ClipID clipId = ClipID::fromRaw(region.sourceId.toRaw());
                    seq->updateClipPosition(clipId, region.positionSample, newLength);
                }
            }
        }
    }
}

void MidiEditorController::setClipLoopPoints(RegionID id,
                                           uint64_t loopStartOffset,
                                           uint64_t loopDuration) {
    (void)id;
    (void)loopStartOffset;
    (void)loopDuration;
    // Loop offset editing can be integrated directly or managed via timeline
}

// ── ISessionChangeListener ─────────────────────────────────────────
void MidiEditorController::onSessionChanging() {
    closeClip();
}

void MidiEditorController::onSessionChanged(composition::IProjectSession* newSession) {
    (void)newSession;
    // Context remains cleared until next openClip()
}

// ── Private Helpers ────────────────────────────────────────────────
composition::ITrackManager* MidiEditorController::getTrackManager() const {
    if (!sessionManager_) return nullptr;
    auto* session = sessionManager_->getActiveSession();
    if (!session) return nullptr;
    return session->getTrackManager();
}

bool MidiEditorController::getActiveRegion(composition::TimelineRegion& outRegion) const {
    auto activeTrackId_ = activeTrackIdAtomic_.load(std::memory_order_acquire);
    auto activeRegionId_ = activeRegionIdAtomic_.load(std::memory_order_acquire);
    if (!sessionManager_ || !activeRegionId_.isValid()) return false;
    auto* session = sessionManager_->getActiveSession();
    if (!session) return false;
    auto* trackManager = session->getTrackManager();
    if (!trackManager) return false;
    auto* playlist = trackManager->getPlaylist(activeTrackId_);
    if (!playlist) return false;

    std::vector<composition::IPlaylist::RegionInfo> scratch(128);
    uint32_t count = playlist->getAllRegions(scratch.data(), static_cast<uint32_t>(scratch.size()));
    if (count > scratch.size()) {
        scratch.resize(count);
        count = playlist->getAllRegions(scratch.data(), static_cast<uint32_t>(scratch.size()));
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (scratch[i].id == activeRegionId_) {
            outRegion = scratch[i].region;
            return true;
        }
    }
    return false;
}

void MidiEditorController::flushActiveNotes() {
    if (!eventQueue_ || activeNotes_.empty()) return;
    
    uint32_t offset = 0;
    if (clockService_) {
        uint64_t cycleStartSteadyTime = clockService_->getCycleStartSteadyTime();
        uint32_t currentNumFrames = clockService_->getCurrentNumFrames();
        
        uint64_t nowSteadyTime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count());
        
        if (nowSteadyTime > cycleStartSteadyTime) {
            uint64_t elapsedNs = nowSteadyTime - cycleStartSteadyTime;
            double sampleRate = clockService_->getSampleRate();
            double nsPerSample = 1e9 / sampleRate;
            offset = static_cast<uint32_t>(elapsedNs / nsPerSample);
            if (offset >= currentNumFrames) {
                offset = currentNumFrames - 1;
            }
        }
    }

    for (const auto& note : activeNotes_) {
        eventQueue_->pushEvent(Layer2::EventHelpers::makeMIDINoteOff(note.targetNode, note.pitch, 0, note.channel, offset));
    }
    activeNotes_.clear();
}

void MidiEditorController::startTimerThread() {
    timerStop_.store(false, std::memory_order_relaxed);
    timerThread_ = std::thread([this]() {
        while (!timerStop_.load(std::memory_order_relaxed)) {
            std::function<void()> taskAction;
            {
                std::unique_lock<std::mutex> lock(timerMutex_);
                if (timerQueue_.empty()) {
                    timerCv_.wait(lock, [this]() {
                        return timerStop_.load(std::memory_order_relaxed) || !timerQueue_.empty();
                    });
                } else {
                    auto now = std::chrono::steady_clock::now();
                    auto topTime = timerQueue_.top().triggerTime;
                    if (now >= topTime) {
                        taskAction = timerQueue_.top().action;
                        timerQueue_.pop();
                    } else {
                        timerCv_.wait_until(lock, topTime, [this]() {
                            return timerStop_.load(std::memory_order_relaxed);
                        });
                    }
                }
            }
            if (taskAction) {
                taskAction();
            }
        }
    });
}

void MidiEditorController::stopTimerThread() {
    {
        std::unique_lock<std::mutex> lock(timerMutex_);
        timerStop_.store(true, std::memory_order_relaxed);
        while (!timerQueue_.empty()) {
            timerQueue_.pop();
        }
    }
    timerCv_.notify_all();
    if (timerThread_.joinable()) {
        timerThread_.join();
    }
}

void MidiEditorController::scheduleTask(uint32_t delayMs, std::function<void()> action) {
    auto triggerTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
    {
        std::unique_lock<std::mutex> lock(timerMutex_);
        timerQueue_.push({triggerTime, std::move(action)});
    }
    timerCv_.notify_one();
}

void MidiEditorController::beginGesture() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* session = sessionManager_->getActiveSession();
    if (session && session->getCommandHistory()) {
        session->getCommandHistory()->beginCompound();
    }
}

void MidiEditorController::endGesture() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* session = sessionManager_->getActiveSession();
    if (session && session->getCommandHistory()) {
        session->getCommandHistory()->endCompound();
    }
}

} // namespace bridge
