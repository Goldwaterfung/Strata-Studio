// src/Core audio engine/transport/itransport.h
#pragma once

#include "common/system_primitives.h"
#include <cstdint>
#include <memory>

// Forward declarations (Layer 2 interfaces)
namespace Layer2 {
    class ITempoService;
    class IStateManager;
}

namespace Layer3 {

//==============================================================================
// TRANSPORT INTERFACE
//==============================================================================

class ITransport {
public:
    //==========================================================================
    // Factory
    //==========================================================================

    // Create transport with specified sample rate
    // Parameters:
    //   sampleRate: Audio sample rate in Hz (44100, 48000, etc.)
    // Returns: Unique pointer to transport instance
    static std::unique_ptr<ITransport> create(uint32_t sampleRate);

    //==========================================================================
    // Transport Control (RT-Safe)
    //==========================================================================

    // Start playback
    // Postcondition: State changes to PLAYING, position advances
    // Thread-safety: RT-safe (atomic state change)
    virtual void play() = 0;

    // Stop playback
    // Postcondition: State changes to STOPPED, position freezes
    // Thread-safety: RT-safe (atomic state change)
    virtual void stop() = 0;

    // Enable recording mode
    // Returns: true if recording started, false if already recording
    // Thread-safety: RT-safe (atomic state change)
    virtual bool record() = 0;

    // Check if recording is armed
    // Returns: true if armed, false otherwise
    // Thread-safety: RT-safe (atomic read)
    virtual bool isRecordArmed() const = 0;

    // Explicitly set record arm state
    // Parameters:
    //   armed: true to arm, false to disarm
    // Thread-safety: RT-safe (atomic write)
    virtual void setRecordArmed(bool armed) = 0;

    // Set transport state directly
    // Thread-safety: RT-safe (atomic state change)
    virtual void setState(TransportState state) = 0;

    // Get current transport state
    // Returns: Current state (STOPPED, PLAYING, RECORDING)
    // Thread-safety: RT-safe (atomic read)
    virtual TransportState getState() const = 0;

    //==========================================================================
    // Position Control (RT-Safe)
    //==========================================================================

    enum class SeekMode : uint8_t {
        IMMEDIATE,      // Jump immediately (may cause glitch)
        BUFFER_SYNC,    // Sync at buffer boundary
        FADE_CROSS      // Crossfade at seek position
    };

    // Seek to position
    // Parameters:
    //   position: Target position in samples
    //   mode: Seek mode (immediate, sync, crossfade)
    // Thread-safety: RT-safe (atomic write)
    virtual void seek(uint64_t position, SeekMode mode) = 0;

    // Get current position in samples
    // Returns: Current position in samples
    // Thread-safety: RT-safe (atomic read)
    virtual uint64_t getPosition() const = 0;

    // Get detailed position with tempo and time signature
    // Returns: Complete position information
    // Thread-safety: RT-safe (atomic reads)
    virtual TransportPosition getDetailedPosition() const = 0;

    // Advance position by numSamples
    // Parameters:
    //   numSamples: Number of samples to advance
    // Returns: true if loop wrapped, false otherwise
    // Postcondition: Position incremented, loop handling applied
    // Thread-safety: RT-safe, wait-free (atomic arithmetic)
    virtual bool advancePosition(uint32_t numSamples) = 0;

    //==========================================================================
    // Loop Control (RT-Safe)
    //==========================================================================

    // Set loop range
    // Parameters:
    //   start: Loop start position in samples
    //   end: Loop end position in samples
    // Thread-safety: RT-safe (atomic write)
    virtual void setLoopRange(uint64_t start, uint64_t end) = 0;

    // Enable/disable looping
    // Parameters:
    //   enabled: true to enable looping, false to disable
    // Thread-safety: RT-safe (atomic write)
    virtual void setLoopEnabled(bool enabled) = 0;

    // Get current loop state
    // Returns: Current loop configuration (by value for thread-safety)
    // Thread-safety: RT-safe (atomic read)
    virtual LoopState getLoopState() const = 0;

    //==========================================================================
    // Metronome Control (RT-Safe)
    //==========================================================================

    // Enable/disable metronome
    // Thread-safety: RT-safe (atomic write)
    virtual void setMetronomeEnabled(bool enabled) = 0;

    // Get current metronome state
    // Returns: true if metronome is active, false otherwise
    // Thread-safety: RT-safe (atomic read)
    virtual bool isMetronomeEnabled() const = 0;

    //==========================================================================
    // Tempo Service Integration (Initialization Only)
    //==========================================================================

    // Attach tempo service for time conversion
    // Precondition: service must outlive transport
    // Thread-safety: NOT thread-safe, call during initialization only
    virtual void setTempoService(Layer2::ITempoService* tempoService) = 0;

    // Attach state manager for snapshots
    // Precondition: manager must outlive transport
    // Thread-safety: NOT thread-safe, call during initialization only
    virtual void setStateManager(Layer2::IStateManager* stateManager) = 0;

    // Update tempo cache (called from non-RT thread)
    // Thread-safety: NOT RT-safe (may allocate)
    virtual void updateTempoCache() = 0;

    //==========================================================================
    // State Snapshots (Non-RT)
    //==========================================================================

    // Create transport state snapshot
    // Returns: Snapshot ID for later restoration
    // Thread-safety: NOT RT-safe (calls IStateManager)
    virtual uint64_t createTransportSnapshot() const = 0;

    // Restore transport state from snapshot
    // Parameters:
    //   snapshotId: Snapshot ID from createTransportSnapshot()
    // Returns: true if restored successfully, false otherwise
    // Thread-safety: NOT RT-safe (calls IStateManager)
    virtual bool restoreTransportSnapshot(uint64_t snapshotId) = 0;

    //==========================================================================
    // Time Conversion (RT-Safe)
    //==========================================================================

    // Convert samples to beats
    // Returns: Position in beats
    // Thread-safety: RT-safe (uses cached tempo)
    virtual double samplesToBeats(uint64_t samples) const = 0;

    // Convert beats to samples
    // Returns: Position in samples
    // Thread-safety: RT-safe (uses cached tempo)
    virtual uint64_t beatsToSamples(double beats) const = 0;

    //==========================================================================
    // Destructor
    //==========================================================================

    virtual ~ITransport() = default;
};

} // namespace Layer3
