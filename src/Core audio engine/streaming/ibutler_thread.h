// src/Core audio engine/streaming/ibutler_thread.h
#pragma once

#include "system_primitives.h"
#include <cstdint>
#include <memory>
#include <functional>

// Forward declaration (Layer 1 interface)
namespace Layer1 {
    class IFileSystem;
    using FileHandle = uint64_t;
}
#include "../Hardware/OS abstraction/common/layer1_primitives.h"

namespace Layer3 {

//==============================================================================
// STREAMING BUFFER INTERFACE
//==============================================================================

class IStreamingBuffer {
public:
    //==========================================================================
    // Buffer State
    //==========================================================================

    enum class BufferState : uint8_t {
        EMPTY,      // No data available
        FILLING,    // Butler thread is filling
        READY,      // Data available for reading
        DRAINING    // Audio thread is draining
    };

    virtual ~IStreamingBuffer() = default;

    //==========================================================================
    // RT Thread Operations (RT-Safe, Wait-Free)
    //==========================================================================

    // Get RT buffer for reading (all channels)
    // Parameters:
    //   readPosition: Current read position in samples
    // Returns: Array of pointers to planar audio data (one per channel), nullptr if underrun
    // Thread-safety: RT-safe, wait-free
    // Note: Caller must NOT cache this pointer - it becomes invalid on next call
    virtual const float* const* getRTBuffer(uint64_t readPosition) = 0;

    // Request buffer refill
    // Parameters:
    //   readPosition: Current read position (triggers refill)
    // Thread-safety: RT-safe, wait-free (atomic flag)
    virtual void requestRefill(uint64_t readPosition) = 0;

    // Provide recorded data (for recording streams)
    // Parameters:
    //   data: Recorded audio data (planar format)
    //   numSamples: Number of samples to record
    // Thread-safety: RT-safe, wait-free
    virtual void provideRecordedData(const float* data, uint32_t numSamples) = 0;

    //==========================================================================
    // Butler Thread Operations (Non-RT-Safe)
    //==========================================================================

    // Associate buffer with a file handle
    // Parameters:
    //   handle: Valid file handle from IFileSystem
    // Thread-safety: NOT RT-safe
    virtual void associateFile(Layer1::FileHandle handle) = 0;

    // Set starting offset in file (in samples)
    // Parameters:
    //   offsetSamples: Offset from start of audio data
    // Thread-safety: NOT RT-safe
    virtual void setTimelineOffset(uint64_t timelineOffset, uint64_t sourceStart) = 0;

    // Set playback ratio for time-stretching (NRT)
    // Thread-safety: NOT RT-safe
    virtual void setPlaybackRatio(float ratio) = 0;

    // Refill buffer asynchronously (called by butler thread)
    // Parameters:
    //   readPosition: Current read position
    //   fs: Filesystem interface for I/O
    // Thread-safety: NOT RT-safe (called from butler thread)
    virtual void refillAsync(uint64_t readPosition, Layer1::IFileSystem* fs) = 0;

    // Flush buffer contents
    // Thread-safety: NOT RT-safe
    virtual void flushAsync() = 0;

    //==========================================================================
    // Query Operations
    //==========================================================================

    // Get current read position
    // Returns: Current read position in samples
    // Thread-safety: RT-safe (atomic read)
    virtual uint64_t getReadPosition() const = 0;

    // Get current buffer state
    // Returns: Current state
    // Thread-safety: RT-safe (atomic read)
    virtual BufferState getState() const = 0;

    // Get available frames for reading
    // Returns: Number of frames available
    // Thread-safety: RT-safe (atomic read)
    virtual uint32_t getAvailableFrames() const = 0;

    // Get total buffer capacity
    // Returns: Total capacity in frames
    // Thread-safety: RT-safe (atomic read)
    virtual uint32_t getTotalCapacity() const = 0;

    // Get number of audio channels in the buffer
    // Returns: Number of channels
    // Thread-safety: RT-safe (read-only configuration)
    virtual uint32_t getNumChannels() const = 0;

    //==========================================================================
    // Configuration
    //==========================================================================

    // Set buffer size
    // Parameters:
    //   numFrames: Buffer capacity in frames
    // Thread-safety: NOT RT-safe (reallocates buffer)
    virtual void setBufferSize(uint32_t numFrames) = 0;

    // Set read-ahead size
    // Parameters:
    //   numFrames: Read-ahead trigger point
    // Thread-safety: NOT RT-safe
    virtual void setReadAheadSize(uint32_t numFrames) = 0;

    // Set target system sample rate
    // Parameters:
    //   sampleRate: System sample rate in Hz
    // Thread-safety: NOT RT-safe
    virtual void setSampleRate(uint32_t sampleRate) = 0;

    // Get current sample rate
    // Returns: Target system sample rate in Hz
    // Thread-safety: RT-safe
    virtual uint32_t getSampleRate() const = 0;

    //==========================================================================
    // Factory
    //==========================================================================

    // Create streaming buffer
    // Parameters:
    //   channels: Number of audio channels
    //   sampleRate: Sample rate in Hz
    // Returns: Unique pointer to buffer instance
    static std::unique_ptr<IStreamingBuffer> create(uint32_t channels, uint32_t sampleRate);
};

//==============================================================================
// BUTLER THREAD INTERFACE
//==============================================================================

class IButlerThread {
public:
    //==========================================================================
    // Factory
    //==========================================================================

    // Create butler thread
    // Returns: Unique pointer to butler thread instance
    static std::unique_ptr<IButlerThread> create();

    //==========================================================================
    // Thread Control
    //==========================================================================

    enum class ButlerState : uint8_t {
        IDLE,          // No work pending
        WORKING,       // Processing I/O
        SHUTTING_DOWN  // Thread exiting
    };

    // Start butler thread
    // Returns: true if started successfully
    // Thread-safety: NOT thread-safe
    virtual bool start(Layer1::WorkgroupHandle workgroupHandle = Layer1::WorkgroupHandle::invalid()) = 0;

    // Stop butler thread
    // Thread-safety: NOT thread-safe (blocks until thread exits)
    virtual void stop() = 0;

    // Set system sample rate and update all registered buffers
    virtual void setSampleRate(float sampleRate) = 0;

    //==========================================================================
    // Buffer Management
    //==========================================================================

    // Attach filesystem interface for file I/O operations
    // Parameters:
    //   filesystem: Filesystem instance (butler does not own)
    // Thread-safety: NOT RT-safe (call during initialization only)
    virtual void attachFileSystem(Layer1::IFileSystem* filesystem) = 0;

    // Register buffer for management
    // Parameters:
    //   buffer: Buffer to register (butler does not own)
    // Returns: true if registered successfully
    // Thread-safety: NOT RT-safe
    virtual bool registerBuffer(IStreamingBuffer* buffer) = 0;

    // Unregister buffer
    // Parameters:
    //   buffer: Buffer to unregister
    // Returns: true if unregistered successfully
    // Thread-safety: NOT RT-safe
    virtual bool unregisterBuffer(IStreamingBuffer* buffer) = 0;

    // Track-to-Buffer mapping registry (for look-ahead matching)
    virtual bool registerBufferForTrack(uint32_t trackId, IStreamingBuffer* buffer) = 0;
    virtual bool unregisterBufferForTrack(uint32_t trackId) = 0;

    // Source-to-Path registry (for background file resolution)
    virtual void registerSourcePath(uint32_t sourceId, const char* filePath) = 0;
    virtual void unregisterSourcePath(uint32_t sourceId) = 0;

    // Transport and Snapshot updates from the RT audio loop
    virtual void updateTransportState(uint64_t positionSample, float sampleRate, bool isPlaying) = 0;
    virtual void updateTimelineSnapshot(const TimelineSnapshot* snapshot) = 0;

    // Region-to-Buffer mapping registry
    virtual void registerBufferForRegion(uint64_t regionId, uint32_t sourceId, IStreamingBuffer* buffer) = 0;
    virtual void unregisterBufferForRegion(uint64_t regionId) = 0;
    virtual IStreamingBuffer* getBufferForRegion(uint64_t regionId, uint32_t sourceId) const = 0;

    //==========================================================================
    // Generic Task Management
    //==========================================================================

    // Schedule a generic background task
    // Parameters:
    //   task: Function to execute on the butler thread
    // Thread-safety: Thread-safe (MPSC queue)
    virtual void scheduleTask(std::function<void()> task) = 0;

    //==========================================================================
    // Wake Signaling
    //==========================================================================

    // Wake butler thread (called from RT thread when buffer needs refill)
    // Thread-safety: RT-safe (signals semaphore)
    virtual void wakeButler() = 0;

    //==========================================================================
    // Query Operations
    //==========================================================================

    // Get current butler state
    // Returns: Current state
    // Thread-safety: RT-safe (atomic read)
    virtual ButlerState getState() const = 0;

    // Get number of pending buffers
    // Returns: Number of buffers waiting for refill
    // Thread-safety: RT-safe (atomic read)
    virtual uint32_t getPendingBufferCount() const = 0;

    //==========================================================================
    // Destructor
    //==========================================================================

    virtual ~IButlerThread() = default;
};

} // namespace Layer3
