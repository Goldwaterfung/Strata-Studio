// src/Core audio engine/streaming/butler_thread_impl.h
#pragma once

#include "ibutler_thread.h"
#include "../Hardware/OS abstraction/filesystem/ifile_system.h"
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <string>
#include <unordered_map>

// Platform-specific semaphore includes
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#else
#include <semaphore.h>
#endif

namespace Layer3 {

class ButlerThreadImpl : public IButlerThread {
public:
    ButlerThreadImpl();
    ~ButlerThreadImpl() override;

    // IButlerThread implementation
    bool start(Layer1::WorkgroupHandle workgroupHandle = Layer1::WorkgroupHandle::invalid()) override;
    void stop() override;
    void setSampleRate(float sampleRate) override;

    void attachFileSystem(Layer1::IFileSystem* filesystem) override;

    bool registerBuffer(IStreamingBuffer* buffer) override;
    bool unregisterBuffer(IStreamingBuffer* buffer) override;

    bool registerBufferForTrack(uint32_t trackId, IStreamingBuffer* buffer) override;
    bool unregisterBufferForTrack(uint32_t trackId) override;

    void registerSourcePath(uint32_t sourceId, const char* filePath) override;
    void unregisterSourcePath(uint32_t sourceId) override;

    void updateTransportState(uint64_t positionSample, float sampleRate, bool isPlaying) override;
    void updateTimelineSnapshot(const TimelineSnapshot* snapshot) override;

    void registerBufferForRegion(uint64_t regionId, uint32_t sourceId, IStreamingBuffer* buffer) override;
    void unregisterBufferForRegion(uint64_t regionId) override;
    IStreamingBuffer* getBufferForRegion(uint64_t regionId, uint32_t sourceId) const override;

    void wakeButler() override;
    void scheduleTask(std::function<void()> task) override;

    ButlerState getState() const override { return state.load(std::memory_order_relaxed); }
    uint32_t getPendingBufferCount() const override;

private:
    void threadLoop();

    std::thread worker;
    std::atomic<bool> isRunning;
    std::atomic<ButlerState> state;
    std::atomic<uint32_t> pendingCount;  // Tracks pending refills
    Layer1::WorkgroupHandle workgroupHandle_{Layer1::WorkgroupHandle::invalid()};

    std::vector<IStreamingBuffer*> registeredBuffers;
    std::mutex bufferMutex;

    std::vector<std::function<void()>> taskQueue;
    std::mutex taskMutex;

#ifdef __APPLE__
    dispatch_semaphore_t wakeSemaphore;  // RT-safe wakeup mechanism (macOS)
#else
    sem_t wakeSemaphore;  // RT-safe wakeup mechanism (Linux/other POSIX)
#endif
    struct RegionBufferMapping {
        std::atomic<uint64_t> regionId{0};
        std::atomic<uint32_t> sourceId{0};
        std::atomic<IStreamingBuffer*> buffer{nullptr};
    };
    static constexpr size_t MAX_REGION_MAPPINGS = 1024;
    RegionBufferMapping regionMappings_[MAX_REGION_MAPPINGS];

    // Track-to-Buffer mapping for lookahead matching
    struct TrackBufferMapping {
        std::atomic<uint32_t> trackId{0};
        std::atomic<IStreamingBuffer*> buffer{nullptr};
    };
    static constexpr size_t MAX_TRACK_MAPPINGS = 1024;
    TrackBufferMapping trackMappings_[MAX_TRACK_MAPPINGS];

    // Registries for source path resolution and active file handles
    struct SourcePathMapping {
        uint32_t sourceId = 0;
        char filePath[512] = {0};
    };
    std::vector<SourcePathMapping> sourcePaths_;
    std::mutex sourcePathsMutex_;

    struct ActiveFileHandleMapping {
        IStreamingBuffer* buffer = nullptr;
        Layer1::FileHandle handle = 0;
    };
    std::vector<ActiveFileHandleMapping> activeFiles_;
    std::mutex activeFilesMutex_;

    // Atomics for tracking the playback engine state asynchronously
    std::atomic<uint64_t> transportPos_{0};
    std::atomic<float> sampleRate_{0.0f};
    std::atomic<bool> isPlaying_{false};
    std::atomic<const TimelineSnapshot*> activeSnapshot_{nullptr};

    Layer1::IFileSystem* filesystem;  // File I/O interface (not owned)
};

} // namespace Layer3
