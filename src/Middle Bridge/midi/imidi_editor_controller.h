// src/Middle Bridge/imidi_editor_controller.h
#pragma once
#include "common/system_primitives.h"
#include "musical_composition/musical_primitives.h"
#include "Middle Bridge/midi/midi_clip_primitives.h"

namespace bridge {

class IMidiEditorController {
public:
    virtual ~IMidiEditorController() = default;

    // ── Active Clip Focus ──────────────────────────────────────────────
    /// Opens a MIDI clip for editing (sets internal focus).
    /// Returns false if the regionId does not reference a MIDI clip.
    virtual bool openClip(TrackID trackId, RegionID regionId) = 0;
    virtual void closeClip() = 0;
    virtual bool hasOpenClip() const = 0;
    virtual RegionID getOpenRegionId() const = 0;
    virtual TrackID  getOpenTrackId() const = 0;

    // ── Note CRUD (NRT / GUI thread only) ─────────────────────────────
    /// All mutations push a ProjectDelta through ICommandHistory (undo/redo free).
    virtual NoteID  addNote(uint8_t pitch, uint8_t velocity, uint8_t channel,
                            uint64_t startFrame, uint64_t endFrame) = 0;
    virtual void    removeNote(NoteID id) = 0;
    virtual void    moveNote(NoteID id, uint8_t newPitch, uint64_t newStartFrame) = 0;
    virtual void    resizeNote(NoteID id, uint64_t newEndFrame) = 0;
    virtual void    setNoteVelocity(NoteID id, uint8_t velocity) = 0;

    // ── Batch / Selection Operations ──────────────────────────────────
    virtual void    removeSelectedNotes(const NoteID* ids, uint32_t count) = 0;
    virtual void    transposeSelectedNotes(const NoteID* ids, uint32_t count,
                                          int8_t semitones) = 0;
    virtual void    shiftSelectedNotes(const NoteID* ids, uint32_t count,
                                       int64_t deltaFrames) = 0;

    // ── Quantization ──────────────────────────────────────────────────
    virtual void    quantizeSelectedNotes(const NoteID* ids, uint32_t count,
                                          uint16_t gridResolutionTicks,
                                          float strength,
                                          bool  quantizeEnds,
                                          int   swingPercentage) = 0;

    // ── Viewport Queries (stack-allocated, zero heap) ──────────────────
    /// Returns notes that overlap [startFrame, endFrame).
    virtual uint32_t getNotesInViewport(
        uint64_t startFrame, uint64_t endFrame,
        composition::MIDINote* outNotes, uint32_t maxNotes) const = 0;

    /// Returns CC points for a specific controller in the viewport.
    virtual uint32_t getCCPointsInViewport(
        uint64_t startFrame, uint64_t endFrame,
        uint8_t controllerNumber,
        VisualCCPoint* outPoints, uint32_t maxPoints) const = 0;

    // ── CC / Expression Editing ────────────────────────────────────────
    virtual void addCCPoint(uint8_t controllerNumber, uint8_t value,
                            uint8_t channel, uint64_t framePosition) = 0;
    virtual void removeCCPointsInRange(uint8_t controllerNumber,
                                       uint64_t startFrame, uint64_t endFrame) = 0;

    // ── MIDI Note Preview (triggers instrument without recording) ──────
    /// Sends a transient NOTE_ON to the track's instrument node for audition.
    /// The NOTE_OFF is automatically scheduled after `durationMs` milliseconds.
    virtual void previewNote(uint8_t pitch, uint8_t velocity,
                             uint8_t channel, uint32_t durationMs) = 0;

    // ── Live MIDI Auditioning (RT-safe, manual duration) ──────────────
    /// Sends a manual sample-accurate NOTE_ON to the active track's instrument node.
    virtual void noteOn(uint8_t pitch, uint8_t velocity, uint8_t channel) = 0;
    /// Sends a manual sample-accurate NOTE_OFF to the active track's instrument node.
    virtual void noteOff(uint8_t pitch, uint8_t channel) = 0;

    // ── Clip Boundary Editing ──────────────────────────────────────────
    virtual void setClipStart(RegionID id, uint64_t newStartFrame) = 0;
    virtual void setClipEnd(RegionID id, uint64_t newEndFrame) = 0;
    virtual void setClipLoopPoints(RegionID id,
                                   uint64_t loopStartOffset,
                                   uint64_t loopDuration) = 0;

    // ── Transaction Gestures ───────────────────────────────────────────
    virtual void beginGesture() = 0;
    virtual void endGesture() = 0;
};

} // namespace bridge
