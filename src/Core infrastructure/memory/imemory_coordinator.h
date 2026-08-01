// imemory_coordinator.h
// Layer 2: Core Infrastructure Services - Memory Coordinator Interface
// Priority-aware buffer pool with wait-free acquisition

#pragma once

#include "system_primitives.h"
#include <memory>
#include <cstdint>

namespace Layer2 {

class IMemoryCoordinator {
public:
    // === Nested Types === //

    enum class ReconfigureResult : uint8_t {
        SUCCESS,
        INSUFFICIENT_MEMORY,
        LOCKED_BUFFERS_IN_USE,
        INVALID_CONFIGURATION
    };

    // Internal priority (maps to Layer1::ThreadPriority for threads)
    enum class MemoryPriority : uint8_t {
        REALTIME,       // Audio thread access (highest priority)
        HIGH,           // DSP worker threads
        NORMAL,         // Main/GUI thread operations
        BACKGROUND      // File I/O, plugin scanning (lowest priority)
    };

    struct PoolConfig {
        uint32_t initialChannels;
        uint32_t initialFramesPerBuffer;
        uint32_t numBuffers;
        uint32_t alignment;
        bool enableMemoryLocking;
        bool enablePriorityInheritance;

        uint32_t maxChannels;
        uint32_t maxFramesPerBuffer;

        uint8_t realtimeBufferRatio;    // % for REALTIME (default: 50)
        uint8_t highBufferRatio;        // % for HIGH (default: 30)
        uint8_t normalBufferRatio;      // % for NORMAL (default: 15)
        // Remaining goes to BACKGROUND (default: 5)
    };

    struct RTStats {
        uint32_t totalBuffers;
        uint32_t availableBuffers;
        uint32_t failedAllocations;
    };

    struct DetailedStats {
        uint32_t totalBuffers;
        uint32_t allocatedBuffers;
        uint32_t peakAllocation;
        uint64_t totalAllocationAttempts;
        uint64_t failedAllocations;
        uint32_t currentMaxChannels;
        uint32_t currentMaxFrames;

        struct TierStats {
            uint32_t totalBuffers;
            uint32_t availableBuffers;
            uint32_t borrowedFromHigher;
            uint32_t borrowedToLower;
            uint64_t failedAllocations;
        } tierStats[4];  // One per MemoryPriority
    };

    // === Buffer Acquisition (RT-SAFE) === //
    // Thread-safety: Wait-free, safe to call from real-time thread

    virtual AudioBufferHandle acquireBuffer(uint32_t numChannels,
                                           uint32_t numFrames,
                                           MemoryPriority priority = MemoryPriority::NORMAL) = 0;

    virtual AudioBufferHandle acquireBufferEx(const AudioBuffer& requestedTemplate,
                                            MemoryPriority priority) = 0;

    virtual void releaseBuffer(const AudioBuffer& buffer) = 0;

    virtual bool isValid() const = 0;

    // === Pool Reconfiguration (NON-RT-SAFE) === //
    // Thread-safety: NOT safe to call from real-time thread
    // Precondition: All buffers must be released before calling

    virtual ReconfigureResult reconfigure(const PoolConfig& newConfig) = 0;

    // === Statistics Query === //

    virtual void getStats_RT(RTStats& outStats) const = 0;
    virtual void getStats_Detailed(DetailedStats& outStats) const = 0;

    // === Priority Inheritance Control === //

    virtual bool setPriorityInheritance(bool enabled) = 0;
    virtual bool isPriorityInheritanceEnabled() const = 0;
    virtual void getPriorityStats_RT(MemoryPriority priority,
                                    RTStats& outStats) const = 0;

    // === Factory === //

    static std::unique_ptr<IMemoryCoordinator> create(const PoolConfig& config);

    virtual ~IMemoryCoordinator() = default;
};

} // namespace Layer2
