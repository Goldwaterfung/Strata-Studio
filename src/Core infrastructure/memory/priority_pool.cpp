// priority_pool.cpp
// Layer 2: Core Infrastructure Services - Priority-Aware Buffer Pool

#include "priority_pool.h"
#include "aligned_allocator.h"
#include <cstring>
#include <new>

namespace Layer2 {

namespace {

// Calculate tier sizes based on ratios
void calculateTierSizes(uint32_t totalBuffers,
                       const IMemoryCoordinator::PoolConfig& config,
                       uint32_t outSizes[4])
{
    // Calculate remaining percentage for BACKGROUND
    uint8_t backgroundRatio = 100 - config.realtimeBufferRatio
                                - config.highBufferRatio
                                - config.normalBufferRatio;

    outSizes[0] = (totalBuffers * config.realtimeBufferRatio) / 100;  // REALTIME
    outSizes[1] = (totalBuffers * config.highBufferRatio) / 100;      // HIGH
    outSizes[2] = (totalBuffers * config.normalBufferRatio) / 100;    // NORMAL
    outSizes[3] = (totalBuffers * backgroundRatio) / 100;             // BACKGROUND

    // Ensure at least 1 buffer per tier (if totalBuffers >= 4)
    uint32_t currentTotal = outSizes[0] + outSizes[1] + outSizes[2] + outSizes[3];
    
    // If we have at least 4 buffers, ensure every tier has at least one
    if (totalBuffers >= 4) {
        for (int i = 0; i < 4; ++i) {
            if (outSizes[i] == 0) {
                // Find a tier to steal from (prefer the one with most buffers)
                int maxTier = 0;
                for (int j = 1; j < 4; ++j) {
                    if (outSizes[j] > outSizes[maxTier]) maxTier = j;
                }
                
                if (outSizes[maxTier] > 1) {
                    outSizes[maxTier]--;
                    outSizes[i]++;
                }
            }
        }
    }
    
    // Final adjustment to ensure sum == totalBuffers
    currentTotal = outSizes[0] + outSizes[1] + outSizes[2] + outSizes[3];
    if (currentTotal < totalBuffers) {
        outSizes[0] += (totalBuffers - currentTotal);
    } else if (currentTotal > totalBuffers) {
        // This shouldn't happen with the logic above, but for safety:
        uint32_t excess = currentTotal - totalBuffers;
        for (int i = 0; i < 4 && excess > 0; ++i) {
            if (outSizes[i] > 1) {
                uint32_t take = std::min(excess, outSizes[i] - 1);
                outSizes[i] -= take;
                excess -= take;
            }
        }
    }
}

} // anonymous namespace

PriorityPool::PriorityPool(uint32_t totalBuffers,
                          const IMemoryCoordinator::PoolConfig& config)
    : buffers(nullptr)
    , totalCapacity(totalBuffers)
    , priorityInheritanceEnabled(config.enablePriorityInheritance)
{
    // Calculate tier sizes
    uint32_t tierSizes[4];
    calculateTierSizes(totalBuffers, config, tierSizes);

    // Initialize tiers
    uint32_t offset = 0;
    for (int i = 0; i < 4; ++i) {
        tiers[i].capacity = tierSizes[i];
        tiers[i].bufferOffset = offset;

        // Allocate free list array
        tiers[i].freeIndices = new std::atomic<uint32_t>[tierSizes[i]];
        for (uint32_t j = 0; j < tierSizes[i]; ++j) {
            // Fill with indices in reverse order (stack)
            tiers[i].freeIndices[j].store(tierSizes[i] - 1 - j, std::memory_order_relaxed);
        }
        tiers[i].freeCount.store(tierSizes[i], std::memory_order_relaxed);

        offset += tierSizes[i];
    }

    // Allocate buffer array
    buffers = static_cast<AudioBuffer*>(AlignedAllocator::allocate(
        sizeof(AudioBuffer) * totalBuffers,
        alignof(AudioBuffer)
    ));

    // Initialize buffers
    for (uint32_t i = 0; i < totalBuffers; ++i) {
        new (&buffers[i]) AudioBuffer();
        buffers[i].numChannels = config.initialChannels;
        buffers[i].numFrames = config.initialFramesPerBuffer;
        buffers[i].bufferId = i;
        buffers[i].flags = 0;
        std::memset(buffers[i].channelData, 0, sizeof(buffers[i].channelData));
    }
}

PriorityPool::~PriorityPool()
{
    if (buffers) {
        for (uint32_t i = 0; i < totalCapacity; ++i) {
            buffers[i].~AudioBuffer();
        }
        AlignedAllocator::deallocate(buffers);
    }

    for (int i = 0; i < 4; ++i) {
        delete[] tiers[i].freeIndices;
    }
}

uint32_t PriorityPool::acquire(IMemoryCoordinator::MemoryPriority priority)
{
    uint32_t tierIndex = static_cast<uint32_t>(getTierIndex(priority));

    // Try to acquire from this tier first
    uint32_t bufferIndex = acquireFromTier(static_cast<uint8_t>(tierIndex));
    if (bufferIndex != UINT32_MAX) {
        return bufferIndex;
    }

    // If priority inheritance is enabled, try borrowing from lower tiers
    if (priorityInheritanceEnabled.load(std::memory_order_acquire)) {
        bufferIndex = borrowFromLowerTier(static_cast<uint8_t>(tierIndex));
        if (bufferIndex != UINT32_MAX) {
            return bufferIndex;
        }
    }

    return UINT32_MAX;  // Pool exhausted
}

uint32_t PriorityPool::acquireFromTier(uint8_t tierIndex)
{
    TierPool& tier = tiers[tierIndex];

    // Use relaxed load for initial check (fast path)
    uint32_t freeCount = tier.freeCount.load(std::memory_order_relaxed);
    if (freeCount == 0) {
        return UINT32_MAX;
    }

    // Try to decrement free count
    uint32_t currentFree = freeCount;
    while (!tier.freeCount.compare_exchange_weak(
        currentFree,
        currentFree - 1,
        std::memory_order_acquire,
        std::memory_order_relaxed))
    {
        if (currentFree == 0) {
            return UINT32_MAX;  // Someone else took the last buffer
        }
    }

    // Get the actual buffer index from the free list
    uint32_t stackPos = currentFree - 1;
    
    // Spin-wait if the index is not yet written by a concurrent release
    uint32_t bufferIndexInTier = UINT32_MAX;
    while ((bufferIndexInTier = tier.freeIndices[stackPos].exchange(UINT32_MAX, std::memory_order_acq_rel)) == UINT32_MAX) {
        // In a real-time context, this spin should be extremely short
        // as it only waits for a single store in release()
#if defined(__x86_64__) || defined(_M_X64)
        _mm_pause();
#elif defined(__arm64__) || defined(__aarch64__)
        asm volatile("yield");
#endif
    }

    return tier.bufferOffset + bufferIndexInTier;
}

uint32_t PriorityPool::borrowFromLowerTier(uint8_t currentTier)
{
    // Try to borrow from lower priority tiers
    for (uint8_t i = currentTier + 1; i < 4; ++i) {
        uint32_t bufferIndex = acquireFromTier(i);
        if (bufferIndex != UINT32_MAX) {
            return bufferIndex;
        }
    }
    return UINT32_MAX;
}

void PriorityPool::release(uint32_t bufferIndex)
{
    if (bufferIndex >= totalCapacity) {
        return;  // Invalid index
    }

    // Determine which tier this buffer belongs to
    uint32_t offset = 0;
    uint8_t foundTierIndex = 0;
    for (int i = 0; i < 4; ++i) {
        if (bufferIndex < offset + tiers[i].capacity) {
            foundTierIndex = static_cast<uint8_t>(i);
            break;
        }
        offset += tiers[i].capacity;
    }

    TierPool& tier = tiers[foundTierIndex];
    uint32_t indexInTier = bufferIndex - tier.bufferOffset;

    // Push back to free list
    // 1. Get position
    uint32_t stackPos = tier.freeCount.fetch_add(1, std::memory_order_relaxed);
    
    // 2. Store the index (making it available for acquire)
    tier.freeIndices[stackPos].store(indexInTier, std::memory_order_release);
}

void PriorityPool::getTierStats(IMemoryCoordinator::MemoryPriority priority,
                                IMemoryCoordinator::RTStats& stats) const
{
    const TierPool& tier = tiers[getTierIndex(priority)];

    stats.totalBuffers = tier.capacity;
    stats.availableBuffers = tier.freeCount.load(std::memory_order_acquire);
    stats.failedAllocations = 0;  // Not tracked at tier level in current implementation
}

uint32_t PriorityPool::getTierIndex(IMemoryCoordinator::MemoryPriority priority) const
{
    switch (priority) {
        case IMemoryCoordinator::MemoryPriority::REALTIME:   return 0;
        case IMemoryCoordinator::MemoryPriority::HIGH:       return 1;
        case IMemoryCoordinator::MemoryPriority::NORMAL:     return 2;
        case IMemoryCoordinator::MemoryPriority::BACKGROUND: return 3;
        default:                                             return 2;
    }
}

} // namespace Layer2
