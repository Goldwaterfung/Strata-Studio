#pragma once
#include "musical_composition/musical_primitives.h"
#include "midi_expression_primitives.h"

namespace Layer2 {
class IEventQueue;
class ITempoService;
}

namespace composition {

class IMIDISequencer {
public:
    virtual ~IMIDISequencer() = default;

    // --- Non-RT Mutations ---
    virtual NoteID addNote(ClipID clipId, const MIDINote& note) = 0;
    virtual void removeNote(NoteID id) = 0;
    virtual void updateNote(NoteID id, const MIDINote& newNote) = 0;

    virtual void addCCPoint(ClipID clipId, const MIDICCPoint& point) = 0;
    virtual void addPitchPoint(ClipID clipId, const MIDIPitchPoint& point) = 0;

    virtual uint32_t getCCPointsInClip(
        ClipID clipId,
        MIDICCPoint* outPoints,
        uint32_t maxPoints
    ) const = 0;

    virtual uint32_t getPitchPointsInClip(
        ClipID clipId,
        MIDIPitchPoint* outPoints,
        uint32_t maxPoints
    ) const = 0;

    virtual void removeCCPointsInClip(
        ClipID clipId
    ) = 0;

    virtual void updateClipPosition(ClipID clipId, uint64_t positionSample, uint64_t sourceLength,
                                    const MusicalPosition& musicalStart = {}) = 0;
    virtual void removeClip(ClipID clipId) = 0;

    /**
     * @brief Recalculate all absolute sample caches (startSample, positionSample) from musical
     *        BBT positions using the provided tempo service. Must be called off the RT thread
     *        whenever the TempoMap changes.
     */
    virtual void recalculateTimeCaches(Layer2::ITempoService* tempoService) = 0;

    /**
     * @brief Set the target DSP node for generated events.
     */
    virtual void setTargetNodeId(NodeID nodeId) = 0;

    // --- RT-Safe Queries & Rendering ---
    
    /**
     * @brief Real-time rendering of notes to MIDI events
     * @thread_safety RT-Safe but NOT thread-safe for concurrent mutation.
     * Assumes notes vector is not modified during rendering.
     * @param startSample Start position of the current processing block
     * @param numSamples Length of the block
     * @param eventQueue Layer 3 queue to push NOTE_ON/OFF events into
     */
    virtual void renderToEvents(
        uint64_t startSample,
        uint32_t numSamples,
        bool loopEnabled,
        uint64_t loopStart,
        uint64_t loopEnd,
        Layer2::IEventQueue* eventQueue
    ) = 0;

    /**
     * @brief Get notes belonging to a specific clip without allocating memory
     * @thread_safety RT-Safe but NOT thread-safe for concurrent mutation.
     * @param outNotes Caller-provided buffer
     */
    virtual uint32_t getNotesInClip(
        ClipID clipId,
        MIDINote* outNotes,
        uint32_t maxNotes
    ) const = 0;
};

} // namespace composition
