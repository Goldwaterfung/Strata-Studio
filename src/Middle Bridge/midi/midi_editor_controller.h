// src/Middle Bridge/midi_editor_controller.h
#pragma once
#include "midi/imidi_editor_controller.h"
#include "project/isession_manager.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "Core infrastructure/bridges/ievent_queue.h"
#include "Core infrastructure/tempo/itempo_service.h"
#include "musical_composition/midi_sequencer/imidi_sequencer.h"
#include <mutex>
#include <queue>
#include <condition_variable>
#include <functional>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

namespace Layer2 {
    class IClockService;
}

namespace bridge {

class MidiEditorController : public IMidiEditorController, public ISessionChangeListener {
public:
    MidiEditorController(
        ISessionManager* sessionManager,
        Layer2::IStringRegistry* stringRegistry,
        Layer2::ITempoService* tempoService = nullptr,
        Layer2::IEventQueue* eventQueue = nullptr,
        Layer2::IClockService* clockService = nullptr
    );
    ~MidiEditorController() override;

    // ── Active Clip Focus ──────────────────────────────────────────────
    bool openClip(TrackID trackId, RegionID regionId) override;
    void closeClip() override;
    bool hasOpenClip() const override;
    RegionID getOpenRegionId() const override;
    TrackID  getOpenTrackId() const override;

    // ── Note CRUD (NRT / GUI thread only) ─────────────────────────────
    NoteID  addNote(uint8_t pitch, uint8_t velocity, uint8_t channel,
                    uint64_t startFrame, uint64_t endFrame) override;
    void    removeNote(NoteID id) override;
    void    moveNote(NoteID id, uint8_t newPitch, uint64_t newStartFrame) override;
    void    resizeNote(NoteID id, uint64_t newEndFrame) override;
    void    setNoteVelocity(NoteID id, uint8_t velocity) override;

    // ── Batch / Selection Operations ──────────────────────────────────
    void    removeSelectedNotes(const NoteID* ids, uint32_t count) override;
    void    transposeSelectedNotes(const NoteID* ids, uint32_t count,
                                  int8_t semitones) override;
    void    shiftSelectedNotes(const NoteID* ids, uint32_t count,
                               int64_t deltaFrames) override;

    // ── Quantization ──────────────────────────────────────────────────
    void    quantizeSelectedNotes(const NoteID* ids, uint32_t count,
                                  uint16_t gridResolutionTicks,
                                  float strength,
                                  bool  quantizeEnds,
                                  int   swingPercentage) override;

    // ── Viewport Queries (stack-allocated, zero heap) ──────────────────
    uint32_t getNotesInViewport(
        uint64_t startFrame, uint64_t endFrame,
        composition::MIDINote* outNotes, uint32_t maxNotes) const override;

    uint32_t getCCPointsInViewport(
        uint64_t startFrame, uint64_t endFrame,
        uint8_t controllerNumber,
        VisualCCPoint* outPoints, uint32_t maxPoints) const override;

    // ── CC / Expression Editing ────────────────────────────────────────
    void addCCPoint(uint8_t controllerNumber, uint8_t value,
                    uint8_t channel, uint64_t framePosition) override;
    void removeCCPointsInRange(uint8_t controllerNumber,
                               uint64_t startFrame, uint64_t endFrame) override;

    // ── MIDI Note Preview ──────────────────────────────────────────────
    void previewNote(uint8_t pitch, uint8_t velocity,
                     uint8_t channel, uint32_t durationMs) override;

    // ── Live MIDI Auditioning (RT-safe, manual duration) ──────────────
    void noteOn(uint8_t pitch, uint8_t velocity, uint8_t channel) override;
    void noteOff(uint8_t pitch, uint8_t channel) override;

    // ── Clip Boundary Editing ──────────────────────────────────────────
    void setClipStart(RegionID id, uint64_t newStartFrame) override;
    void setClipEnd(RegionID id, uint64_t newEndFrame) override;
    void setClipLoopPoints(RegionID id,
                           uint64_t loopStartOffset,
                           uint64_t loopDuration) override;

    void beginGesture() override;
    void endGesture() override;

    // ── ISessionChangeListener ─────────────────────────────────────────
    void onSessionChanging() override;
    void onSessionChanged(composition::IProjectSession* newSession) override;

private:
    composition::ITrackManager* getTrackManager() const;
    bool getActiveRegion(composition::TimelineRegion& outRegion) const;

    void startTimerThread();
    void stopTimerThread();
    void scheduleTask(uint32_t delayMs, std::function<void()> action);
    void flushActiveNotes();

    ISessionManager*            sessionManager_;
    Layer2::ITempoService*      tempoService_;
    Layer2::IEventQueue*        eventQueue_;
    Layer2::IClockService*      clockService_ = nullptr;

    std::atomic<TrackID>                     activeTrackIdAtomic_;
    std::atomic<RegionID>                    activeRegionIdAtomic_;
    std::atomic<composition::IMIDISequencer*> activeSequencerAtomic_;
    mutable std::mutex          mutex_;

    struct ActiveNoteKey {
        NodeID targetNode;
        uint8_t channel;
        uint8_t pitch;

        bool operator==(const ActiveNoteKey& o) const {
            return targetNode == o.targetNode && channel == o.channel && pitch == o.pitch;
        }
    };
    std::vector<ActiveNoteKey> activeNotes_;

    // Standard C++ NRT Note Preview Scheduler
    struct ScheduledTask {
        std::chrono::steady_clock::time_point triggerTime;
        std::function<void()> action;

        bool operator>(const ScheduledTask& other) const {
            return triggerTime > other.triggerTime;
        }
    };
    std::mutex timerMutex_;
    std::condition_variable timerCv_;
    std::thread timerThread_;
    std::atomic<bool> timerStop_{false};
    std::priority_queue<ScheduledTask, std::vector<ScheduledTask>, std::greater<ScheduledTask>> timerQueue_;
};

} // namespace bridge
