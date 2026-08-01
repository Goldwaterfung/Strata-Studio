#pragma once
#include "timeline/itimeline_controller.h"
#include "Core audio engine/transport/itransport.h"
#include "Core infrastructure/tempo/itempo_service.h"
#include "project/isession_manager.h"

namespace Layer1 { class IAudioDriver; }
namespace Layer2 { class IStringRegistry; }
namespace composition { struct ProjectDelta; }

namespace bridge {
class ISessionManager;
class IRecordingController;
class IAutomationController;

class TimelineController : public ITimelineController, public ISessionChangeListener {
public:
    TimelineController(
        Layer3::ITransport* transport,
        Layer2::ITempoService* tempoService
    );
    ~TimelineController() override = default;

    void setSessionManager(ISessionManager* sm) {
        if (sessionManager_) {
            sessionManager_->unregisterChangeListener(this);
        }
        sessionManager_ = sm;
        if (sessionManager_) {
            sessionManager_->registerChangeListener(this);
        }
    }
    void setStringRegistry(Layer2::IStringRegistry* sr) { stringRegistry_ = sr; }
    void setAudioDriver(Layer1::IAudioDriver* driver) { audioDriver_ = driver; }
    void setAutomationController(IAutomationController* ac) { automationController_ = ac; }
    void setRecordingController(IRecordingController* rc) { recordingController_ = rc; }

    // --- Transport Playback Control ---
    void togglePlay() override;
    void play() override;
    void stop() override;
    void setRecordArmed(bool armed) override;

    // --- Seek / Locate ---
    void seekToFrame(uint64_t framePosition) override;
    void seekToTimeSeconds(double seconds) override;
    void seekToMusicalGrid(double bar, double beat) override;

    // --- Tempo / Signature and Looping Zones ---
    void setBPM(double bpm) override;
    void setTimeSignature(VisualTimeSignature timeSig) override;
    void setLoopRange(uint64_t startFrame, uint64_t endFrame) override;
    void setLoopEnabled(bool enabled) override;
    bool isTempoAutomated() const override;
    void removeTempoPoint(uint64_t framePosition) override;
    void addTempoPoint(uint64_t framePosition, double bpm) override;
    uint32_t getTempoPoints(uint64_t startFrame, uint64_t endFrame,
                            VisualTempoPoint* outPoints, uint32_t maxCount) const override;

    // --- Thread-Safe Playhead & State Queries ---
    uint64_t getCurrentFrame() const override;
    double getCurrentSeconds() const override;
    bool isPlaying() const override;
    bool isRecording() const override;
    bool isRecordArmed() const override;
    bool isLooping() const override;
    double getBPM() const override;
    
    // --- Named Markers ---
    void     addMarker(uint64_t framePosition, const char* label, uint32_t colorARGB) override;
    void     removeMarker(const MarkerUUID& uuid) override;
    void     updateMarker(const MarkerUUID& uuid, uint64_t framePosition, const char* label, uint32_t colorARGB) override;
    uint32_t getMarkersInRange(uint64_t startFrame, uint64_t endFrame,
                               VisualMarker* outMarkers, uint32_t maxCount) const override;

    // --- View Conversion Helpers ---
    double pixelsToFrames(float pixels, float zoomFactor) const override;
    float framesToPixels(uint64_t frames, float zoomFactor) const override;
    double getSampleRate() const override;
    uint64_t getLoopStart() const override;
    uint64_t getLoopEnd() const override;

    // --- Playback Mode (Song vs Pattern) ---
    void setPlaybackMode(PlaybackMode mode) override;
    PlaybackMode getPlaybackMode() const override;

    // --- BBT Query ---
    void getCurrentBBT(uint32_t& bar, uint32_t& beat, uint32_t& tick) const override;

    // --- BBT/Tick/Frame Conversions ---
    uint64_t samplesToTicks(uint64_t samples) const override;
    uint64_t ticksToSamples(uint64_t ticks) const override;
    uint32_t getTicksPerBeat() const override;
    void frameToBBT(uint64_t frame, uint32_t& bar, uint32_t& beat, uint32_t& tick) const override;
    uint64_t bbtToFrame(uint32_t bar, uint32_t beat, uint32_t tick) const override;
    void getTimeSignatureAtFrame(uint64_t frame, uint8_t& numerator, uint8_t& denominator) const override;

    // --- Metronome ---
    void setMetronomeEnabled(bool enabled) override;
    bool isMetronomeEnabled() const override;

    // --- Count-in / Pre-count ---
    void setCountInEnabled(bool enabled) override;
    bool isCountInEnabled() const override;
    void setCountInBars(uint8_t bars) override;
    uint8_t getCountInBars() const override;

    // --- ISessionChangeListener ---
    void onSessionChanging() override;
    void onSessionChanged(composition::IProjectSession* newSession) override;

private:
    void applyTempoTimelineDelta(const composition::ProjectDelta& delta, bool isUndo);

    Layer3::ITransport* transport_;
    Layer2::ITempoService* tempoService_;
    ISessionManager* sessionManager_ = nullptr;
    Layer2::IStringRegistry* stringRegistry_ = nullptr;
    Layer1::IAudioDriver* audioDriver_ = nullptr;
    IAutomationController* automationController_ = nullptr;
    IRecordingController* recordingController_ = nullptr;

    PlaybackMode playbackMode_ = PlaybackMode::SONG;
    bool metronomeEnabled_ = false;
    bool countInEnabled_ = false;
    uint8_t countInBars_ = 2;
    bool isApplyingDelta_ = false;
};

} // namespace bridge
