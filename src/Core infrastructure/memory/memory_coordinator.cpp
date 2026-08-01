// memory_coordinator.cpp
// Layer 2: Core Infrastructure Services - Memory Coordinator Implementation

#include "imemory_coordinator.h"
#include "buffer_pool.h"
#include "Hardware/OS abstraction/threading/ithread_manager.h"
#include <memory>

namespace Layer2 {

namespace {

// Helper: Map MemoryPriority to Layer1::ThreadPriority
// Note: Currently unused but reserved for future thread priority integration
Layer1::ThreadPriority mapMemoryPriority(IMemoryCoordinator::MemoryPriority memPrio)
{
    (void)memPrio;  // Suppress unused warning for now
    switch (memPrio) {
        case IMemoryCoordinator::MemoryPriority::REALTIME:
            return Layer1::ThreadPriority::REALTIME;
        case IMemoryCoordinator::MemoryPriority::HIGH:
            return Layer1::ThreadPriority::HIGH;
        case IMemoryCoordinator::MemoryPriority::NORMAL:
            return Layer1::ThreadPriority::NORMAL;
        case IMemoryCoordinator::MemoryPriority::BACKGROUND:
            return Layer1::ThreadPriority::LOW;
        default:
            return Layer1::ThreadPriority::NORMAL;
    }
}

} // anonymous namespace

// Reference the function to suppress unused warning
// This will be used when implementing background thread expansion
void force_reference_mapMemoryPriority()
{
    (void)mapMemoryPriority;  // Suppress unused warning
}

class MemoryCoordinatorImpl : public IMemoryCoordinator {
private:
    std::unique_ptr<BufferPool> pool;
    std::unique_ptr<Layer1::IThreadManager> threadManager;
    PoolConfig currentConfig;
    std::atomic<bool> priorityInheritanceEnabled{false};

public:
    explicit MemoryCoordinatorImpl(const PoolConfig& config)
        : currentConfig(config)
    {
        // Create Layer 1 thread manager
        threadManager = Layer1::IThreadManager::create();

        // Create and initialize buffer pool
        pool = std::make_unique<BufferPool>(config);
        if (pool) {
            pool->initialize();
        }

        priorityInheritanceEnabled.store(config.enablePriorityInheritance,
                                        std::memory_order_release);
    }

    ~MemoryCoordinatorImpl() override = default;

    // === Buffer Acquisition (RT-SAFE) === //

    AudioBufferHandle acquireBuffer(uint32_t numChannels,
                                   uint32_t numFrames,
                                   MemoryPriority priority) override
    {
        if (!pool || !pool->isValid()) {
            return AudioBufferHandle();
        }
        return pool->acquire(this, numChannels, numFrames, priority);
    }

    AudioBufferHandle acquireBufferEx(const AudioBuffer& requestedTemplate,
                                    MemoryPriority priority) override
    {
        if (!pool || !pool->isValid()) {
            return AudioBufferHandle();
        }
        return pool->acquire(this,
                           requestedTemplate.numChannels,
                           requestedTemplate.numFrames,
                           priority);
    }

    void releaseBuffer(const AudioBuffer& buffer) override
    {
        if (pool) {
            pool->release(buffer);
        }
    }

    bool isValid() const override
    {
        return pool && pool->isValid();
    }

    // === Pool Reconfiguration (NON-RT-SAFE) === //

    ReconfigureResult reconfigure(const PoolConfig& newConfig) override
    {
        if (!pool) {
            return ReconfigureResult::INVALID_CONFIGURATION;
        }

        ReconfigureResult result = pool->reconfigure(newConfig);
        if (result == ReconfigureResult::SUCCESS) {
            currentConfig = newConfig;
            priorityInheritanceEnabled.store(newConfig.enablePriorityInheritance,
                                            std::memory_order_release);
        }
        return result;
    }

    // === Statistics Query === //

    void getStats_RT(RTStats& outStats) const override
    {
        if (pool) {
            pool->getStats_RT(outStats);
        } else {
            outStats.totalBuffers = 0;
            outStats.availableBuffers = 0;
            outStats.failedAllocations = 0;
        }
    }

    void getStats_Detailed(DetailedStats& outStats) const override
    {
        if (pool) {
            pool->getStats_Detailed(outStats);
        } else {
            std::memset(&outStats, 0, sizeof(outStats));
        }
    }

    // === Priority Inheritance Control === //

    bool setPriorityInheritance(bool enabled) override
    {
        priorityInheritanceEnabled.store(enabled, std::memory_order_release);
        // Note: In production, this would also update the PriorityPool
        return true;
    }

    bool isPriorityInheritanceEnabled() const override
    {
        return priorityInheritanceEnabled.load(std::memory_order_acquire);
    }

    void getPriorityStats_RT(MemoryPriority priority,
                            RTStats& outStats) const override
    {
        (void)priority;  // TODO: Implement per-priority statistics
        if (pool) {
            // Get tier-specific stats
            pool->getStats_RT(outStats);  // Current implementation returns aggregate
        } else {
            outStats.totalBuffers = 0;
            outStats.availableBuffers = 0;
            outStats.failedAllocations = 0;
        }
    }

private:
    // Prevent copying
    MemoryCoordinatorImpl(const MemoryCoordinatorImpl&) = delete;
    MemoryCoordinatorImpl& operator=(const MemoryCoordinatorImpl&) = delete;
};

// === Factory Method === //

std::unique_ptr<IMemoryCoordinator> IMemoryCoordinator::create(
    const PoolConfig& config)
{
    return std::make_unique<MemoryCoordinatorImpl>(config);
}

} // namespace Layer2
