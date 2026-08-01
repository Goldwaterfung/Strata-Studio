// priority_pool.h
// Layer 2: Core Infrastructure Services - Priority-Aware Buffer Pool
// Wait-free buffer pool with 4-tier priority system

#pragma once

#include "imemory_coordinator.h"
#include <atomic>
#include <cstdint>

namespace Layer2 {

// Priority-aware buffer pool with wait-free acquisition
// Thread-safety: All public methods are wait-free and thread-safe
class PriorityPool {
private:
    struct TierPool {
        alignas(64) std::atomic<uint32_t> freeCount;
        alignas(64) std::atomic<uint32_t>* freeIndices;
        uint32_t capacity;
        uint32_t bufferOffset;
    };

    TierPool tiers[4];  // REALTIME, HIGH, NORMAL, BACKGROUND
    AudioBuffer* buffers;
    uint32_t totalCapacity;

    // Priority inheritance state
    std::atomic<bool> priorityInheritanceEnabled;

public:
    PriorityPool(uint32_t totalBuffers, const IMemoryCoordinator::PoolConfig& config);
    ~PriorityPool();

    // Wait-free acquisition with priority-aware borrowing
    // Returns: buffer index or UINT32_MAX if exhausted
    // Thread-safety: Wait-free, safe to call from real-time thread
    uint32_t acquire(IMemoryCoordinator::MemoryPriority priority);

    // Wait-free release
    // Thread-safety: Wait-free, safe to call from real-time thread
    void release(uint32_t bufferIndex);

    // Query tier statistics (RT-safe)
    // Thread-safety: Wait-free, safe to call from real-time thread
    void getTierStats(IMemoryCoordinator::MemoryPriority priority,
                     IMemoryCoordinator::RTStats& stats) const;

    // Get total capacity
    uint32_t getCapacity() const { return totalCapacity; }

    // Get buffer object by index
    AudioBuffer& getBuffer(uint32_t index) { return buffers[index]; }

    // Enable/disable priority inheritance
    void setPriorityInheritance(bool enabled) {
        priorityInheritanceEnabled.store(enabled, std::memory_order_release);
    }

    bool isPriorityInheritanceEnabled() const {
        return priorityInheritanceEnabled.load(std::memory_order_acquire);
    }

private:
    uint32_t acquireFromTier(uint8_t tierIndex);
    uint32_t borrowFromLowerTier(uint8_t currentTier);
    uint32_t getTierIndex(IMemoryCoordinator::MemoryPriority priority) const;

    // Prevent copying
    PriorityPool(const PriorityPool&) = delete;
    PriorityPool& operator=(const PriorityPool&) = delete;
};

} // namespace Layer2
