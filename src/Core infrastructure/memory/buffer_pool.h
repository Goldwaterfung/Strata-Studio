// buffer_pool.h
// Layer 2: Core Infrastructure Services - Buffer Pool Management
// Manages audio buffer lifecycle with generation counters

#pragma once

#include "imemory_coordinator.h"
#include "priority_pool.h"
#include "aligned_allocator.h"
#include <vector>
#include <atomic>

namespace Layer2 {

// Buffer pool with generation counter for ABA prevention
// Thread-safety:
//   - acquire/release: Wait-free, RT-safe
//   - reconfigure: NOT RT-safe, requires exclusive access
class BufferPool {
private:
    struct BufferMetadata {
        alignas(64) std::atomic<uint32_t> generation;
        uint32_t maxChannels;
        uint32_t maxFrames;
        alignas(64) std::atomic<bool> inUse;
    };

    std::vector<AudioBuffer> buffers;
    BufferMetadata* metadata;  // Raw array (atomics not compatible with vector)
    uint32_t metadataCapacity;
    std::unique_ptr<PriorityPool> pool;
    IMemoryCoordinator::PoolConfig config;
    std::atomic<uint64_t> totalAllocations{0};
    std::atomic<uint64_t> failedAllocations{0};
    std::atomic<uint32_t> peakAllocation{0};
    std::atomic<bool> initialized{false};

public:
    explicit BufferPool(const IMemoryCoordinator::PoolConfig& cfg);
    ~BufferPool();

    // Initialize pool (allocate all buffers upfront)
    // Returns: true if initialization succeeded
    // Thread-safety: NOT RT-safe, call during startup only
    bool initialize();

    // Acquire buffer with generation counter for ABA prevention
    // Returns: AudioBufferHandle (invalid if acquisition failed)
    // Thread-safety: Wait-free, safe to call from real-time thread
    AudioBufferHandle acquire(IMemoryCoordinator* coordinator,
                             uint32_t numChannels,
                             uint32_t numFrames,
                             IMemoryCoordinator::MemoryPriority priority);

    // Release buffer back to pool
    // Thread-safety: Wait-free, safe to call from real-time thread
    void release(const AudioBuffer& buffer);

    // Reconfigure for different buffer sizes (NON-RT-SAFE)
    // Precondition: All buffers must be released
    // Returns: ReconfigureResult indicating success/failure
    IMemoryCoordinator::ReconfigureResult reconfigure(
        const IMemoryCoordinator::PoolConfig& newConfig);

    // Statistics queries
    // Thread-safety: RT-safe (wait-free reads)
    void getStats_RT(IMemoryCoordinator::RTStats& outStats) const;
    void getStats_Detailed(IMemoryCoordinator::DetailedStats& outStats) const;

    bool isValid() const { return initialized.load(std::memory_order_acquire); }

private:
    bool allocateBufferData(AudioBuffer& buffer);
    void freeBufferData(AudioBuffer& buffer);

    // Prevent copying
    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;
};

} // namespace Layer2
