#pragma once
#include "imidi_sequencer.h"
#include "musical_composition/command_history/delta_primitives.h"
#include "Core infrastructure/tempo/itempo_service.h"
#include <vector>
#include <atomic>
#include <memory>

namespace composition {
class ICommandHistory;

class MIDISequencerImpl : public IMIDISequencer {
public:
    MIDISequencerImpl(TrackID trackId, ICommandHistory* history);

    TrackID getTrackId() const { return trackId_; }

    NoteID addNote(ClipID clipId, const MIDINote& note) override;
    void removeNote(NoteID id) override;
    void updateNote(NoteID id, const MIDINote& newNote) override;
    
    void setTargetNodeId(NodeID nodeId) override { targetNodeId_ = nodeId; }

    void addCCPoint(ClipID clipId, const MIDICCPoint& point) override;
    void addPitchPoint(ClipID clipId, const MIDIPitchPoint& point) override;

    uint32_t getCCPointsInClip(ClipID clipId, MIDICCPoint* outPoints, uint32_t maxPoints) const override;
    uint32_t getPitchPointsInClip(ClipID clipId, MIDIPitchPoint* outPoints, uint32_t maxPoints) const override;
    void removeCCPointsInClip(ClipID clipId) override;

    void updateClipPosition(ClipID clipId, uint64_t positionSample, uint64_t sourceLength,
                            const MusicalPosition& musicalStart = {}) override;
    void removeClip(ClipID clipId) override;
    void recalculateTimeCaches(Layer2::ITempoService* tempoService) override;

    void renderToEvents(uint64_t startSample, uint32_t numSamples, bool loopEnabled, uint64_t loopStart, uint64_t loopEnd, Layer2::IEventQueue* eventQueue) override;
    uint32_t getNotesInClip(ClipID clipId, MIDINote* outNotes, uint32_t maxNotes) const override;

    void applyDelta(const ProjectDelta& delta, bool isUndo);
    void copyFrom(const MIDISequencerImpl* other);

    // Raw getters for arrangement cloning
    struct NoteEntry {
        NoteID noteId;
        ClipID clipId;
        MIDINote note;
    };

    struct CCEntry {
        ClipID clipId;
        MIDICCPoint point;
    };

    struct PitchEntry {
        ClipID clipId;
        MIDIPitchPoint point;
    };

    struct ClipPositionEntry {
        ClipID clipId;
        uint64_t positionSample;   // Derived cache — recalculate from startPosition on TempoMap change
        uint64_t sourceLength;
        MusicalPosition startPosition{}; // Primary musical-time anchor
    };

    // Deserialization restore methods
    void restoreClipPosition(const ClipPositionEntry& entry);
    void restoreNote(const NoteEntry& entry);
    void restoreCCPoint(const CCEntry& entry);
    void restorePitchPoint(const PitchEntry& entry);

    const std::vector<NoteEntry>& getRawNotes() const { return notes_; }
    const std::vector<CCEntry>& getRawCCPoints() const { return ccPoints_; }
    const std::vector<PitchEntry>& getRawPitchPoints() const { return pitchPoints_; }
    const std::vector<ClipPositionEntry>& getRawClipPositions() const { return clipPositions_; }

private:
    TrackID trackId_;
    ICommandHistory* history_;
    NodeID targetNodeId_;



    std::vector<NoteEntry> notes_;
    std::vector<CCEntry> ccPoints_;
    std::vector<PitchEntry> pitchPoints_;
    std::vector<ClipPositionEntry> clipPositions_;
    uint32_t nextIdCounter_ = 0;

    struct RTNotesBuffer {
        std::vector<NoteEntry> notes;
        std::vector<CCEntry> ccPoints;
        std::vector<PitchEntry> pitchPoints;
        std::vector<ClipPositionEntry> clipPositions;
    };
    std::unique_ptr<RTNotesBuffer> rtBuffers_[3];
    std::atomic<RTNotesBuffer*> activeBuffer_{nullptr};
    std::atomic<RTNotesBuffer*> pendingBuffer_{nullptr};


    void syncRTBuffer();

    NoteID generateNextId();
    NoteID addNoteInternal(ClipID clipId, const MIDINote& note, NoteID forcedId, bool pushDelta);
    void removeNoteInternal(NoteID id, bool pushDelta);
    void updateNoteInternal(NoteID id, const MIDINote& newNote, bool pushDelta);

    void addCCPointInternal(ClipID clipId, const MIDICCPoint& point, bool pushDelta);
    void removeCCPointInternal(ClipID clipId, const MIDICCPoint& point, bool pushDelta);
    void removeCCPointsInClipInternal(ClipID clipId, bool pushDelta);

    void addPitchPointInternal(ClipID clipId, const MIDIPitchPoint& point, bool pushDelta);
    void removePitchPointInternal(ClipID clipId, const MIDIPitchPoint& point, bool pushDelta);
};

} // namespace composition
