// buffer_pool.cpp
// Layer 2: Core Infrastructure Services - Buffer Pool Management

#include "buffer_pool.h"
#include <cstring>
#include <new>

namespace Layer2 {

BufferPool::BufferPool(const IMemoryCoordinator::PoolConfig& cfg)
    : metadata(nullptr)
    , metadataCapacity(0)
    , pool(nullptr)
    , config(cfg)
{
}

BufferPool::~BufferPool()
{
    if (pool) {
        // Free all buffer data
        for (size_t i = 0; i < buffers.size(); ++i) {
            if (buffers[i].channelData[0] != nullptr) {
                freeBufferData(buffers[i]);
            }
        }
    }

    // Deallocate metadata array
    if (metadata) {
        for (uint32_t i = 0; i < metadataCapacity; ++i) {
            metadata[i].~BufferMetadata();
        }
        ::operator delete(metadata, std::align_val_t{alignof(BufferMetadata)});
        metadata = nullptr;
    }
}

bool BufferPool::initialize()
{
    if (initialized.load(std::memory_order_acquire)) {
        return true;  // Already initialized
    }

    // Create priority pool
    pool = std::make_unique<PriorityPool>(config.numBuffers, config);
    if (!pool) {
        return false;
    }

    // Reserve space for buffers
    buffers.reserve(config.numBuffers);

    // Allocate metadata array directly (atomics not compatible with vector)
    metadataCapacity = config.numBuffers;
    void* rawMemory = ::operator new(sizeof(BufferMetadata) * metadataCapacity, std::align_val_t{alignof(BufferMetadata)});
    metadata = static_cast<BufferMetadata*>(rawMemory);

    // Initialize each metadata entry
    for (uint32_t i = 0; i < metadataCapacity; ++i) {
        new (&metadata[i]) BufferMetadata();
        metadata[i].generation.store(1, std::memory_order_relaxed);
        metadata[i].maxChannels = config.initialChannels;
        metadata[i].maxFrames = config.initialFramesPerBuffer;
        metadata[i].inUse.store(false, std::memory_order_relaxed);
    }

    initialized.store(true, std::memory_order_release);
    return true;
}

AudioBufferHandle BufferPool::acquire(IMemoryCoordinator* coordinator,
                                     uint32_t numChannels,
                                     uint32_t numFrames,
                                     IMemoryCoordinator::MemoryPriority priority)
{
    (void)numChannels;  // Reserved for future size validation
    (void)numFrames;    // Reserved for future size validation

    if (!initialized.load(std::memory_order_acquire)) {
        return AudioBufferHandle();  // Invalid handle
    }

    totalAllocations.fetch_add(1, std::memory_order_relaxed);

    // Try to acquire buffer from priority pool
    uint32_t bufferIndex = pool->acquire(priority);
    if (bufferIndex == UINT32_MAX) {
        failedAllocations.fetch_add(1, std::memory_order_relaxed);
        return AudioBufferHandle();  // Pool exhausted
    }

    // Update metadata
    if (bufferIndex >= metadataCapacity) {
        failedAllocations.fetch_add(1, std::memory_order_relaxed);
        return AudioBufferHandle();
    }

    BufferMetadata& meta = metadata[bufferIndex];
    // Increment generation counter for ABA prevention (currently unused)
    meta.generation.fetch_add(1, std::memory_order_relaxed);

    // Update current allocation count
    uint32_t currentAlloc = 0;
    for (uint32_t i = 0; i < metadataCapacity; ++i) {
        if (metadata[i].inUse.load(std::memory_order_relaxed)) {
            ++currentAlloc;
        }
    }

    // Update peak
    uint32_t currentPeak = peakAllocation.load(std::memory_order_relaxed);
    while (currentAlloc > currentPeak &&
           !peakAllocation.compare_exchange_weak(currentPeak, currentAlloc,
                                                 std::memory_order_release)) {
        // Retry
    }

    // Mark as in use
    meta.inUse.store(true, std::memory_order_release);

    // Get buffer pointer from the pool's permanent storage
    AudioBuffer& buffer = pool->getBuffer(bufferIndex);
    
    // Ensure metadata is consistent (in case of reconfig)
    buffer.numChannels = config.initialChannels;
    buffer.numFrames = config.initialFramesPerBuffer;
    buffer.bufferId = bufferIndex;

    return AudioBufferHandle(coordinator, &buffer);
}

void BufferPool::release(const AudioBuffer& buffer)
{
    if (!initialized.load(std::memory_order_acquire)) {
        return;
    }

    uint32_t bufferIndex = buffer.bufferId;
    if (bufferIndex >= metadataCapacity) {
        return;  // Invalid index
    }

    BufferMetadata& meta = metadata[bufferIndex];
    meta.inUse.store(false, std::memory_order_release);

    // Release back to priority pool
    pool->release(bufferIndex);
}

bool BufferPool::allocateBufferData(AudioBuffer& buffer)
{
    // Allocate channel data (planar format)
    for (uint32_t ch = 0; ch < buffer.numChannels; ++ch) {
        buffer.channelData[ch] = static_cast<float*>(
            AlignedAllocator::allocate(
                buffer.numFrames * sizeof(float),
                config.alignment
            )
        );

        if (!buffer.channelData[ch]) {
            // Allocation failed, clean up previously allocated channels
            for (uint32_t prevCh = 0; prevCh < ch; ++prevCh) {
                AlignedAllocator::deallocate(buffer.channelData[prevCh]);
                buffer.channelData[prevCh] = nullptr;
            }
            return false;
        }

        // Initialize to silence
        std::memset(buffer.channelData[ch], 0, buffer.numFrames * sizeof(float));

        // Lock memory if requested
        if (config.enableMemoryLocking) {
            AlignedAllocator::lockMemory(buffer.channelData[ch],
                                        buffer.numFrames * sizeof(float));
        }
    }

    return true;
}

void BufferPool::freeBufferData(AudioBuffer& buffer)
{
    for (uint32_t ch = 0; ch < buffer.numChannels; ++ch) {
        if (buffer.channelData[ch]) {
            if (config.enableMemoryLocking) {
                AlignedAllocator::unlockMemory(buffer.channelData[ch],
                                             buffer.numFrames * sizeof(float));
            }
            AlignedAllocator::deallocate(buffer.channelData[ch]);
            buffer.channelData[ch] = nullptr;
        }
    }
}

IMemoryCoordinator::ReconfigureResult BufferPool::reconfigure(
    const IMemoryCoordinator::PoolConfig& newConfig)
{
    // Check if any buffers are in use
    for (uint32_t i = 0; i < metadataCapacity; ++i) {
        if (metadata[i].inUse.load(std::memory_order_acquire)) {
            return IMemoryCoordinator::ReconfigureResult::LOCKED_BUFFERS_IN_USE;
        }
    }

    // Validate configuration
    if (newConfig.maxChannels < config.initialChannels ||
        newConfig.maxFramesPerBuffer < config.initialFramesPerBuffer) {
        return IMemoryCoordinator::ReconfigureResult::INVALID_CONFIGURATION;
    }

    // Free existing buffer data
    for (size_t i = 0; i < buffers.size(); ++i) {
        if (buffers[i].channelData[0] != nullptr) {
            freeBufferData(buffers[i]);
        }
    }

    // Update configuration
    config = newConfig;

    // Recreate priority pool
    pool = std::make_unique<PriorityPool>(config.numBuffers, config);

    // Reallocate metadata if needed
    if (newConfig.numBuffers != metadataCapacity) {
        // Deallocate old metadata
        for (uint32_t i = 0; i < metadataCapacity; ++i) {
            metadata[i].~BufferMetadata();
        }
        ::operator delete(metadata, std::align_val_t{alignof(BufferMetadata)});

        // Allocate new metadata
        metadataCapacity = newConfig.numBuffers;
        void* rawMemory = ::operator new(sizeof(BufferMetadata) * metadataCapacity, std::align_val_t{alignof(BufferMetadata)});
        metadata = static_cast<BufferMetadata*>(rawMemory);

        // Initialize new metadata
        for (uint32_t i = 0; i < metadataCapacity; ++i) {
            new (&metadata[i]) BufferMetadata();
            metadata[i].generation.store(1, std::memory_order_relaxed);
            metadata[i].maxChannels = config.initialChannels;
            metadata[i].maxFrames = config.initialFramesPerBuffer;
            metadata[i].inUse.store(false, std::memory_order_relaxed);
        }
    } else {
        // Update existing metadata
        for (uint32_t i = 0; i < metadataCapacity; ++i) {
            metadata[i].maxChannels = config.maxChannels;
            metadata[i].maxFrames = config.maxFramesPerBuffer;
        }
    }

    // Reallocate buffers with new configuration
    for (size_t i = 0; i < buffers.size(); ++i) {
        buffers[i].numChannels = config.initialChannels;
        buffers[i].numFrames = config.initialFramesPerBuffer;

        if (!allocateBufferData(buffers[i])) {
            return IMemoryCoordinator::ReconfigureResult::INSUFFICIENT_MEMORY;
        }
    }

    return IMemoryCoordinator::ReconfigureResult::SUCCESS;
}

void BufferPool::getStats_RT(IMemoryCoordinator::RTStats& outStats) const
{
    if (!pool) {
        outStats.totalBuffers = 0;
        outStats.availableBuffers = 0;
        outStats.failedAllocations = 0;
        return;
    }

    // Aggregate stats from all tiers
    outStats.totalBuffers = config.numBuffers;
    outStats.availableBuffers = 0;
    outStats.failedAllocations = static_cast<uint32_t>(
        failedAllocations.load(std::memory_order_relaxed)
    );

    // Sum available buffers from all tiers
    for (int i = 0; i < 4; ++i) {
        IMemoryCoordinator::RTStats tierStats;
        pool->getTierStats(static_cast<IMemoryCoordinator::MemoryPriority>(i), tierStats);
        outStats.availableBuffers += tierStats.availableBuffers;
    }
}

void BufferPool::getStats_Detailed(IMemoryCoordinator::DetailedStats& outStats) const
{
    outStats.totalBuffers = config.numBuffers;
    outStats.allocatedBuffers = 0;
    outStats.peakAllocation = peakAllocation.load(std::memory_order_relaxed);
    outStats.totalAllocationAttempts = totalAllocations.load(std::memory_order_relaxed);
    outStats.failedAllocations = failedAllocations.load(std::memory_order_relaxed);
    outStats.currentMaxChannels = config.initialChannels;
    outStats.currentMaxFrames = config.initialFramesPerBuffer;

    // Count allocated buffers
    for (uint32_t i = 0; i < metadataCapacity; ++i) {
        if (metadata[i].inUse.load(std::memory_order_relaxed)) {
            ++outStats.allocatedBuffers;
        }
    }

    // Get per-tier stats
    for (int i = 0; i < 4; ++i) {
        IMemoryCoordinator::RTStats tierStats;
        pool->getTierStats(static_cast<IMemoryCoordinator::MemoryPriority>(i), tierStats);

        outStats.tierStats[i].totalBuffers = tierStats.totalBuffers;
        outStats.tierStats[i].availableBuffers = tierStats.availableBuffers;
        outStats.tierStats[i].borrowedFromHigher = 0;  // Not tracked in current implementation
        outStats.tierStats[i].borrowedToLower = 0;      // Not tracked in current implementation
        outStats.tierStats[i].failedAllocations = tierStats.failedAllocations;
    }
}

} // namespace Layer2
