#pragma once
#include <cstdint>
#include "common/system_primitives.h"

namespace bridge {

struct VisualTimeSignature {
    uint8_t numerator;
    uint8_t denominator;
};

/**
 * @brief POD representation of a named marker (time signature label / song marker).
 */
struct VisualMarker {
    MarkerUUID uuid;
    uint64_t framePosition;
    char     label[MAX_NAME_LENGTH];
    uint32_t colorARGB;
    uint32_t markerNumber;
    double   locationSeconds;
    char     timecode[32]; // Formatted: HH:MM:SS:FF
};

struct VisualTempoPoint {
    uint64_t framePosition;
    double bpm;
};

/**
 * @brief Controller interface for controlling the global transport and playhead state
 */
class ITimelineController {
public:
    virtual ~ITimelineController() = default;

    // --- Transport Playback Control ---
    virtual void togglePlay() = 0;
    virtual void play() = 0;
    virtual void stop() = 0;
    virtual void setRecordArmed(bool armed) = 0;

    // --- Seek / Locate ---
    virtual void seekToFrame(uint64_t framePosition) = 0;
    virtual void seekToTimeSeconds(double seconds) = 0;
    virtual void seekToMusicalGrid(double bar, double beat) = 0;

    // --- Tempo / Signature and Looping Zones ---
    virtual void setBPM(double bpm) = 0;
    virtual void setTimeSignature(VisualTimeSignature timeSig) = 0;
    virtual void setLoopRange(uint64_t startFrame, uint64_t endFrame) = 0;
    virtual void setLoopEnabled(bool enabled) = 0;
    virtual bool isTempoAutomated() const = 0;
    virtual void removeTempoPoint(uint64_t framePosition) = 0;
    virtual void addTempoPoint(uint64_t framePosition, double bpm) = 0;
    virtual uint32_t getTempoPoints(uint64_t startFrame, uint64_t endFrame,
                                    VisualTempoPoint* outPoints, uint32_t maxCount) const = 0;

    // --- Thread-Safe Playhead & State Queries ---
    virtual uint64_t getCurrentFrame() const = 0;
    virtual double getCurrentSeconds() const = 0;
    virtual bool isPlaying() const = 0;
    virtual bool isRecording() const = 0;
    virtual bool isRecordArmed() const = 0;
    virtual bool isLooping() const = 0;
    virtual double getBPM() const = 0;
    
    // --- Named Markers ---
    virtual void     addMarker(uint64_t framePosition, const char* label, uint32_t colorARGB) = 0;
    virtual void     removeMarker(const MarkerUUID& uuid) = 0;
    virtual void     updateMarker(const MarkerUUID& uuid, uint64_t framePosition, const char* label, uint32_t colorARGB) = 0;
    virtual uint32_t getMarkersInRange(uint64_t startFrame, uint64_t endFrame,
                                       VisualMarker* outMarkers, uint32_t maxCount) const = 0;

    // --- View Conversion Helpers ---
    virtual double pixelsToFrames(float pixels, float zoomFactor) const = 0;
    virtual float framesToPixels(uint64_t frames, float zoomFactor) const = 0;
    virtual double getSampleRate() const = 0;
    virtual uint64_t getLoopStart() const = 0;
    virtual uint64_t getLoopEnd() const = 0;

    // --- Playback Mode (Song vs Pattern) ---
    virtual void setPlaybackMode(PlaybackMode mode) = 0;
    virtual PlaybackMode getPlaybackMode() const = 0;

    // --- BBT Query ---
    virtual void getCurrentBBT(uint32_t& bar, uint32_t& beat, uint32_t& tick) const = 0;

    // --- BBT/Tick/Frame Conversions ---
    virtual uint64_t samplesToTicks(uint64_t samples) const = 0;
    virtual uint64_t ticksToSamples(uint64_t ticks) const = 0;
    virtual uint32_t getTicksPerBeat() const = 0;
    virtual void frameToBBT(uint64_t frame, uint32_t& bar, uint32_t& beat, uint32_t& tick) const = 0;
    virtual uint64_t bbtToFrame(uint32_t bar, uint32_t beat, uint32_t tick) const = 0;
    virtual void getTimeSignatureAtFrame(uint64_t frame, uint8_t& numerator, uint8_t& denominator) const = 0;

    // --- Metronome ---
    virtual void setMetronomeEnabled(bool enabled) = 0;
    virtual bool isMetronomeEnabled() const = 0;

    // --- Count-in / Pre-count ---
    virtual void setCountInEnabled(bool enabled) = 0;
    virtual bool isCountInEnabled() const = 0;
    virtual void setCountInBars(uint8_t bars) = 0;
    virtual uint8_t getCountInBars() const = 0;
};

} // namespace bridge
