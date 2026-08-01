#include "midi_sequencer_impl.h"
#include "midi_commands.h"
#include "musical_composition/command_history/icommand_history.h"
#include "Core infrastructure/bridges/ievent_queue.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace composition {

MIDISequencerImpl::MIDISequencerImpl(TrackID trackId, ICommandHistory* history) 
    : trackId_(trackId), history_(history) {
    for (int i = 0; i < 3; ++i) {
        rtBuffers_[i] = std::make_unique<RTNotesBuffer>();
        rtBuffers_[i]->notes.reserve(4096);
        rtBuffers_[i]->ccPoints.reserve(8192);
        rtBuffers_[i]->pitchPoints.reserve(4096);
        rtBuffers_[i]->clipPositions.reserve(64);
    }
    activeBuffer_.store(rtBuffers_[0].get(), std::memory_order_release);
}

NoteID MIDISequencerImpl::addNote(ClipID clipId, const MIDINote& note) {
    return addNoteInternal(clipId, note, {0, 0}, true);
}

void MIDISequencerImpl::removeNote(NoteID id) {
    removeNoteInternal(id, true);
}

NoteID MIDISequencerImpl::addNoteInternal(ClipID clipId, const MIDINote& note, NoteID forcedId, bool pushDelta) {
    NoteID id = (forcedId.id != 0) ? forcedId : generateNextId();
    MIDINote noteWithId = note;
    noteWithId.noteId = id;
    
    NoteEntry entry{ id, clipId, noteWithId };
    notes_.push_back(entry);

    if (pushDelta && history_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::MIDI_SEQUENCER;
        delta.operationType = MidiOps::ADD_NOTE;
        delta.targetId = handleToUint64(trackId_);
        
        AddNotePayload payload{ clipId, noteWithId };
        delta.newStateSize = sizeof(AddNotePayload);
        std::memcpy(delta.newState, &payload, sizeof(AddNotePayload));
        
        history_->pushDelta(delta);
    }

    syncRTBuffer();
    return id;
}

void MIDISequencerImpl::removeNoteInternal(NoteID id, bool pushDelta) {
    auto it = std::find_if(notes_.begin(), notes_.end(),
        [&](const NoteEntry& e) { return e.noteId == id; });
    
    if (it != notes_.end()) {
        if (pushDelta && history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::MIDI_SEQUENCER;
            delta.operationType = MidiOps::REMOVE_NOTE;
            delta.targetId = handleToUint64(trackId_);
            
            AddNotePayload payload{ it->clipId, it->note };
            delta.oldStateSize = sizeof(AddNotePayload);
            std::memcpy(delta.oldState, &payload, sizeof(AddNotePayload));
            
            history_->pushDelta(delta);
        }
        notes_.erase(it);
        syncRTBuffer();
    }
}

void MIDISequencerImpl::updateNote(NoteID id, const MIDINote& newNote) {
    updateNoteInternal(id, newNote, true);
}

void MIDISequencerImpl::updateNoteInternal(NoteID id, const MIDINote& newNote, bool pushDelta) {
    auto it = std::find_if(notes_.begin(), notes_.end(),
        [&](const NoteEntry& e) { return e.noteId == id; });
    
    if (it != notes_.end()) {
        if (pushDelta && history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::MIDI_SEQUENCER;
            delta.operationType = MidiOps::UPDATE_NOTE;
            delta.targetId = handleToUint64(id);
            
            AddNotePayload oldP{ it->clipId, it->note };
            delta.oldStateSize = sizeof(AddNotePayload);
            std::memcpy(delta.oldState, &oldP, sizeof(AddNotePayload));
            
            AddNotePayload newP{ it->clipId, newNote };
            newP.note.noteId = id;
            delta.newStateSize = sizeof(AddNotePayload);
            std::memcpy(delta.newState, &newP, sizeof(AddNotePayload));
            
            history_->pushDelta(delta);
        }
        
        it->note = newNote;
        it->note.noteId = id;
        syncRTBuffer();
    }
}

void MIDISequencerImpl::syncRTBuffer() {
    RTNotesBuffer* active = activeBuffer_.load(std::memory_order_relaxed);
    RTNotesBuffer* pending = pendingBuffer_.load(std::memory_order_relaxed);
    RTNotesBuffer* free = nullptr;
    
    for (int i = 0; i < 3; ++i) {
        RTNotesBuffer* b = rtBuffers_[i].get();
        if (b != active && b != pending) {
            free = b;
            break;
        }
    }
    
    if (!free) {
        for (int i = 0; i < 3; ++i) {
            if (rtBuffers_[i].get() != active) {
                free = rtBuffers_[i].get();
                break;
            }
        }
    }
    
    if (free) {
        free->notes = notes_;
        free->ccPoints = ccPoints_;
        free->pitchPoints = pitchPoints_;
        free->clipPositions = clipPositions_;
        pendingBuffer_.store(free, std::memory_order_release);
    }
}

void MIDISequencerImpl::addCCPoint(ClipID clipId, const MIDICCPoint& point) {
    addCCPointInternal(clipId, point, true);
}

void MIDISequencerImpl::addCCPointInternal(ClipID clipId, const MIDICCPoint& point, bool pushDelta) {
    ccPoints_.push_back({ clipId, point });

    if (pushDelta && history_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::MIDI_SEQUENCER;
        delta.operationType = MidiOps::ADD_CC;
        delta.targetId = handleToUint64(trackId_);
        
        AddCCPayload payload{ clipId, point };
        delta.newStateSize = sizeof(AddCCPayload);
        std::memcpy(delta.newState, &payload, sizeof(AddCCPayload));
        
        history_->pushDelta(delta);
    }

    syncRTBuffer();
}

void MIDISequencerImpl::addPitchPoint(ClipID clipId, const MIDIPitchPoint& point) {
    addPitchPointInternal(clipId, point, true);
}

void MIDISequencerImpl::addPitchPointInternal(ClipID clipId, const MIDIPitchPoint& point, bool pushDelta) {
    pitchPoints_.push_back({ clipId, point });

    if (pushDelta && history_) {
        ProjectDelta delta{};
        delta.subsystemId = SubsystemID::MIDI_SEQUENCER;
        delta.operationType = MidiOps::ADD_PITCH;
        delta.targetId = handleToUint64(trackId_);
        
        AddPitchPayload payload{ clipId, point };
        delta.newStateSize = sizeof(AddPitchPayload);
        std::memcpy(delta.newState, &payload, sizeof(AddPitchPayload));
        
        history_->pushDelta(delta);
    }

    syncRTBuffer();
}

void MIDISequencerImpl::removeCCPointInternal(ClipID clipId, const MIDICCPoint& point, bool pushDelta) {
    auto it = std::find_if(ccPoints_.begin(), ccPoints_.end(), [&](const CCEntry& cc) {
        return cc.clipId == clipId &&
               cc.point.samplePosition == point.samplePosition &&
               cc.point.controllerNumber == point.controllerNumber &&
               cc.point.channel == point.channel &&
               cc.point.value == point.value;
    });

    if (it != ccPoints_.end()) {
        if (pushDelta && history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::MIDI_SEQUENCER;
            delta.operationType = MidiOps::REMOVE_CC;
            delta.targetId = handleToUint64(trackId_);
            
            AddCCPayload payload{ clipId, it->point };
            delta.oldStateSize = sizeof(AddCCPayload);
            std::memcpy(delta.oldState, &payload, sizeof(AddCCPayload));
            
            history_->pushDelta(delta);
        }
        ccPoints_.erase(it);
        syncRTBuffer();
    }
}

void MIDISequencerImpl::removePitchPointInternal(ClipID clipId, const MIDIPitchPoint& point, bool pushDelta) {
    auto it = std::find_if(pitchPoints_.begin(), pitchPoints_.end(), [&](const PitchEntry& pb) {
        return pb.clipId == clipId &&
               pb.point.samplePosition == point.samplePosition &&
               pb.point.channel == point.channel &&
               pb.point.value == point.value;
    });

    if (it != pitchPoints_.end()) {
        if (pushDelta && history_) {
            ProjectDelta delta{};
            delta.subsystemId = SubsystemID::MIDI_SEQUENCER;
            delta.operationType = MidiOps::REMOVE_PITCH;
            delta.targetId = handleToUint64(trackId_);
            
            AddPitchPayload payload{ clipId, it->point };
            delta.oldStateSize = sizeof(AddPitchPayload);
            std::memcpy(delta.oldState, &payload, sizeof(AddPitchPayload));
            
            history_->pushDelta(delta);
        }
        pitchPoints_.erase(it);
        syncRTBuffer();
    }
}

uint32_t MIDISequencerImpl::getCCPointsInClip(ClipID clipId, MIDICCPoint* outPoints, uint32_t maxPoints) const {
    uint32_t count = 0;
    for (const auto& cc : ccPoints_) {
        if (cc.clipId == clipId) {
            if (count < maxPoints) {
                outPoints[count] = cc.point;
                count++;
            } else {
                break;
            }
        }
    }
    return count;
}

uint32_t MIDISequencerImpl::getPitchPointsInClip(ClipID clipId, MIDIPitchPoint* outPoints, uint32_t maxPoints) const {
    uint32_t count = 0;
    for (const auto& pb : pitchPoints_) {
        if (pb.clipId == clipId) {
            if (count < maxPoints) {
                outPoints[count] = pb.point;
                count++;
            } else {
                break;
            }
        }
    }
    return count;
}

void MIDISequencerImpl::removeCCPointsInClip(ClipID clipId) {
    removeCCPointsInClipInternal(clipId, true);
}

void MIDISequencerImpl::removeCCPointsInClipInternal(ClipID clipId, bool pushDelta) {
    if (pushDelta && history_) {
        for (const auto& cc : ccPoints_) {
            if (cc.clipId == clipId) {
                ProjectDelta delta{};
                delta.subsystemId = SubsystemID::MIDI_SEQUENCER;
                delta.operationType = MidiOps::REMOVE_CC;
                delta.targetId = handleToUint64(trackId_);
                
                AddCCPayload payload{ clipId, cc.point };
                delta.oldStateSize = sizeof(AddCCPayload);
                std::memcpy(delta.oldState, &payload, sizeof(AddCCPayload));
                
                history_->pushDelta(delta);
            }
        }
    }

    auto it = std::remove_if(ccPoints_.begin(), ccPoints_.end(), [&](const CCEntry& cc) {
        return cc.clipId == clipId;
    });
    ccPoints_.erase(it, ccPoints_.end());
    syncRTBuffer();
}

uint32_t MIDISequencerImpl::getNotesInClip(ClipID clipId, MIDINote* outNotes, uint32_t maxNotes) const {
    uint32_t count = 0;
    for (const auto& entry : notes_) {
        if (entry.clipId == clipId) {
            if (count < maxNotes) {
                outNotes[count] = entry.note;
                count++;
            } else {
                break;
            }
        }
    }
    return count;
}

void MIDISequencerImpl::updateClipPosition(ClipID clipId, uint64_t positionSample, uint64_t sourceLength,
                                          const MusicalPosition& musicalStart) {
    auto it = std::find_if(clipPositions_.begin(), clipPositions_.end(),
        [&](const ClipPositionEntry& e) { return e.clipId == clipId; });
        
    if (it != clipPositions_.end()) {
        it->positionSample = positionSample;
        it->sourceLength = sourceLength;
        it->startPosition = musicalStart;
    } else {
        clipPositions_.push_back({ clipId, positionSample, sourceLength, musicalStart });
    }
    syncRTBuffer();
}

void MIDISequencerImpl::removeClip(ClipID clipId) {
    auto posIt = std::remove_if(clipPositions_.begin(), clipPositions_.end(),
        [&](const ClipPositionEntry& e) { return e.clipId == clipId; });
    clipPositions_.erase(posIt, clipPositions_.end());

    auto noteIt = std::remove_if(notes_.begin(), notes_.end(),
        [&](const NoteEntry& e) { return e.clipId == clipId; });
    notes_.erase(noteIt, notes_.end());

    auto ccIt = std::remove_if(ccPoints_.begin(), ccPoints_.end(),
        [&](const CCEntry& e) { return e.clipId == clipId; });
    ccPoints_.erase(ccIt, ccPoints_.end());

    auto pitchIt = std::remove_if(pitchPoints_.begin(), pitchPoints_.end(),
        [&](const PitchEntry& e) { return e.clipId == clipId; });
    pitchPoints_.erase(pitchIt, pitchPoints_.end());

    syncRTBuffer();
}

void MIDISequencerImpl::renderToEvents(uint64_t /*startSample*/, uint32_t /*numSamples*/, bool /*loopEnabled*/, uint64_t /*loopStart*/, uint64_t /*loopEnd*/, Layer2::IEventQueue* /*eventQueue*/) {
    // Check if UI thread has pushed a new pending buffer, and atomically swap it in
    RTNotesBuffer* newActive = pendingBuffer_.exchange(nullptr, std::memory_order_acq_rel);
    if (newActive) {
        activeBuffer_.store(newActive, std::memory_order_release);
    }
}

NoteID MIDISequencerImpl::generateNextId() {
    return { ++nextIdCounter_, 1 };
}

void MIDISequencerImpl::applyDelta(const ProjectDelta& delta, bool isUndo) {
    AddNotePayload payload;
    AddCCPayload ccPayload;
    AddPitchPayload pitchPayload;
    
    switch (delta.operationType) {
        case MidiOps::ADD_NOTE:
            std::memcpy(&payload, delta.newState, sizeof(AddNotePayload));
            if (isUndo) removeNoteInternal(payload.note.noteId, false);
            else addNoteInternal(payload.clipId, payload.note, payload.note.noteId, false);
            break;
        case MidiOps::REMOVE_NOTE:
            std::memcpy(&payload, delta.oldState, sizeof(AddNotePayload));
            if (isUndo) addNoteInternal(payload.clipId, payload.note, payload.note.noteId, false);
            else removeNoteInternal(payload.note.noteId, false);
            break;
        case MidiOps::UPDATE_NOTE:
            std::memcpy(&payload, isUndo ? delta.oldState : delta.newState, sizeof(AddNotePayload));
            updateNoteInternal(payload.note.noteId, payload.note, false);
            break;
        case MidiOps::ADD_CC:
            std::memcpy(&ccPayload, delta.newState, sizeof(AddCCPayload));
            if (isUndo) removeCCPointInternal(ccPayload.clipId, ccPayload.point, false);
            else addCCPointInternal(ccPayload.clipId, ccPayload.point, false);
            break;
        case MidiOps::REMOVE_CC:
            std::memcpy(&ccPayload, delta.oldState, sizeof(AddCCPayload));
            if (isUndo) addCCPointInternal(ccPayload.clipId, ccPayload.point, false);
            else removeCCPointInternal(ccPayload.clipId, ccPayload.point, false);
            break;
        case MidiOps::ADD_PITCH:
            std::memcpy(&pitchPayload, delta.newState, sizeof(AddPitchPayload));
            if (isUndo) removePitchPointInternal(pitchPayload.clipId, pitchPayload.point, false);
            else addPitchPointInternal(pitchPayload.clipId, pitchPayload.point, false);
            break;
        case MidiOps::REMOVE_PITCH:
            std::memcpy(&pitchPayload, delta.oldState, sizeof(AddPitchPayload));
            if (isUndo) addPitchPointInternal(pitchPayload.clipId, pitchPayload.point, false);
            else removePitchPointInternal(pitchPayload.clipId, pitchPayload.point, false);
            break;
    }
}

void MIDISequencerImpl::copyFrom(const MIDISequencerImpl* other) {
    if (!other) return;
    notes_ = other->notes_;
    ccPoints_ = other->ccPoints_;
    pitchPoints_ = other->pitchPoints_;
    clipPositions_ = other->clipPositions_;
    nextIdCounter_ = other->nextIdCounter_;
    syncRTBuffer();
}

void MIDISequencerImpl::recalculateTimeCaches(Layer2::ITempoService* tempoService) {
    if (!tempoService) return;

    // Recalculate clip start positions from musical anchors
    for (auto& clip : clipPositions_) {
        if (clip.startPosition.totalTicks > 0 || clip.startPosition.bar > 0) {
            Layer2::BBTPosition bbt(
                clip.startPosition.bar,
                clip.startPosition.beat,
                clip.startPosition.tick
            );
            clip.positionSample = tempoService->bbtToSamples(bbt);
        }
    }

    uint32_t ticksPerBeat = tempoService->getTicksPerBeat();

    // Recalculate all note start/end sample caches from musical anchors
    for (auto& entry : notes_) {
        MIDINote& note = entry.note;

        // Recalculate startSample from startPosition if it has been set
        if (note.startPosition.bar > 0 || note.startPosition.totalTicks > 0) {
            Layer2::BBTPosition startBBT(
                note.startPosition.bar,
                note.startPosition.beat,
                note.startPosition.tick
            );
            note.startSample = tempoService->bbtToSamples(startBBT);
        }

        // Recalculate endSample from endPosition if it has been set
        if (note.endPosition.bar > 0 || note.endPosition.totalTicks > 0) {
            Layer2::BBTPosition endBBT(
                note.endPosition.bar,
                note.endPosition.beat,
                note.endPosition.tick
            );
            note.endSample = tempoService->bbtToSamples(endBBT);
            // Keep durationSample consistent
            if (note.endSample >= note.startSample) {
                note.durationSample = note.endSample - note.startSample;
            }
        }

        // Recalculate offsetSample relative to parent clip positionSample
        auto clipIt = std::find_if(clipPositions_.begin(), clipPositions_.end(),
            [&](const ClipPositionEntry& c) { return c.clipId == entry.clipId; });
        if (clipIt != clipPositions_.end()) {
            note.offsetSample = (note.startSample >= clipIt->positionSample)
                ? (note.startSample - clipIt->positionSample)
                : 0;
        } else {
            note.offsetSample = 0;
        }
    }

    // Recalculate CC Point samplePosition from absoluteTickPosition
    for (auto& cc : ccPoints_) {
        auto clipIt = std::find_if(clipPositions_.begin(), clipPositions_.end(),
            [&](const ClipPositionEntry& c) { return c.clipId == cc.clipId; });
        if (clipIt != clipPositions_.end() && ticksPerBeat > 0) {
            double beats = static_cast<double>(cc.point.absoluteTickPosition) / ticksPerBeat;
            uint64_t absoluteSample = tempoService->beatsToSamples(beats);
            cc.point.samplePosition = (absoluteSample >= clipIt->positionSample)
                ? (absoluteSample - clipIt->positionSample)
                : 0;
        }
    }

    // Recalculate Pitch Bend Point samplePosition from absoluteTickPosition
    for (auto& pb : pitchPoints_) {
        auto clipIt = std::find_if(clipPositions_.begin(), clipPositions_.end(),
            [&](const ClipPositionEntry& c) { return c.clipId == pb.clipId; });
        if (clipIt != clipPositions_.end() && ticksPerBeat > 0) {
            double beats = static_cast<double>(pb.point.absoluteTickPosition) / ticksPerBeat;
            uint64_t absoluteSample = tempoService->beatsToSamples(beats);
            pb.point.samplePosition = (absoluteSample >= clipIt->positionSample)
                ? (absoluteSample - clipIt->positionSample)
                : 0;
        }
    }

    syncRTBuffer();
}

void MIDISequencerImpl::restoreClipPosition(const ClipPositionEntry& entry) {
    clipPositions_.push_back(entry);
    syncRTBuffer();
}

void MIDISequencerImpl::restoreNote(const NoteEntry& entry) {
    notes_.push_back(entry);
    if (entry.noteId.id > nextIdCounter_) {
        nextIdCounter_ = entry.noteId.id;
    }
    syncRTBuffer();
}

void MIDISequencerImpl::restoreCCPoint(const CCEntry& entry) {
    ccPoints_.push_back(entry);
    syncRTBuffer();
}

void MIDISequencerImpl::restorePitchPoint(const PitchEntry& entry) {
    pitchPoints_.push_back(entry);
    syncRTBuffer();
}

} // namespace composition
