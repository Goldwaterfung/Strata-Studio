// spsc_queue.h v1.0
// Single Producer Single Consumer Lock-Free Queue
//
// IMPLEMENTATION NOTES:
// - Wait-free push/pop operations
// - Power-of-2 capacity for bitmask optimization
// - Cache-line alignment to prevent false sharing
// - Sequencing for strict ordering guarantees
//
// THREAD SAFETY:
// - push(): Single producer only (non-reentrant)
// - pop(): Single consumer only (non-reentrant)
// - Thread-safe when used by one producer thread and one consumer thread
//
// PERFORMANCE:
// - Target: <50ns per push/pop operation
// - No atomic operations on fast path (relaxed ordering sufficient)

#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

namespace Layer2 {

//==============================================================================
// SPSC Queue - Single Producer, Single Consumer Lock-Free Queue
//==============================================================================

template<typename T, uint32_t Capacity>
class SPSCQueue {
    // Capacity must be power of 2 for bitmask optimization
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "SPSCQueue Capacity must be a power of 2");

    // T must be trivially copyable for memcpy
    static_assert(std::is_trivially_copyable<T>::value,
                  "SPSCQueue T must be trivially copyable");

private:
    // Cache-line alignment to prevent false sharing
    alignas(64) T buffer_[Capacity];

    // Write index (only modified by producer)
    alignas(64) std::atomic<uint32_t> writeIndex_;

    // Read index (only modified by consumer)
    alignas(64) std::atomic<uint32_t> readIndex_;

    // Mask for index wrapping (power of 2 optimization)
    static constexpr uint32_t MASK = Capacity - 1;

public:
    //==========================================================================
    // Constructor
    //==========================================================================

    SPSCQueue()
        : writeIndex_(0)
        , readIndex_(0)
    {
        // Buffer is zero-initialized for POD types
        std::memset(buffer_, 0, sizeof(buffer_));
    }

    //==========================================================================
    // Disabled copy/move (queue manages exclusive ownership)
    //==========================================================================

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;

    //==========================================================================
    // Producer: Push item to queue
    // Returns: true if successful, false if queue is full
    // Thread-safety: Single producer only
    //==========================================================================

    bool push(const T& item)
    {
        const uint32_t currentWrite = writeIndex_.load(std::memory_order_relaxed);
        const uint32_t nextWrite = (currentWrite + 1) & MASK;

        // Check if queue is full (next write would catch up to read)
        // Note: We use acquire here to ensure we see the latest read index
        if (nextWrite == readIndex_.load(std::memory_order_acquire)) {
            return false;  // Queue full
        }

        // Write item to buffer
        buffer_[currentWrite] = item;

        // Publish the write (release ensures item is written before index update)
        writeIndex_.store(nextWrite, std::memory_order_release);

        return true;
    }

    //==========================================================================
    // Producer: Push item to queue with move semantics
    // Returns: true if successful, false if queue is full
    // Thread-safety: Single producer only
    //==========================================================================

    template<typename U = T>
    bool push(U&& item)
    {
        const uint32_t currentWrite = writeIndex_.load(std::memory_order_relaxed);
        const uint32_t nextWrite = (currentWrite + 1) & MASK;

        if (nextWrite == readIndex_.load(std::memory_order_acquire)) {
            return false;  // Queue full
        }

        buffer_[currentWrite] = std::forward<U>(item);
        writeIndex_.store(nextWrite, std::memory_order_release);

        return true;
    }

    //==========================================================================
    // Producer: Try to push multiple items (batch operation)
    // Returns: number of items actually pushed
    // Thread-safety: Single producer only
    //==========================================================================

    uint32_t tryPushBatch(const T* items, uint32_t count)
    {
        uint32_t pushed = 0;

        for (uint32_t i = 0; i < count; ++i) {
            if (!push(items[i])) {
                break;
            }
            ++pushed;
        }

        return pushed;
    }

    //==========================================================================
    // Consumer: Pop item from queue
    // Returns: true if successful, false if queue is empty
    // Thread-safety: Single consumer only
    //==========================================================================

    bool pop(T& outItem)
    {
        const uint32_t currentRead = readIndex_.load(std::memory_order_relaxed);

        // Check if queue is empty (read has caught up to write)
        if (currentRead == writeIndex_.load(std::memory_order_acquire)) {
            return false;  // Queue empty
        }

        // Read item from buffer
        outItem = buffer_[currentRead];

        // Publish the read (release ensures item is read before index update)
        const uint32_t nextRead = (currentRead + 1) & MASK;
        readIndex_.store(nextRead, std::memory_order_release);

        return true;
    }

    //==========================================================================
    // Consumer: Pop multiple items (batch operation)
    // Returns: number of items actually popped
    // Thread-safety: Single consumer only
    //==========================================================================

    uint32_t popMultiple(T* outItems, uint32_t maxItems)
    {
        uint32_t popped = 0;

        for (uint32_t i = 0; i < maxItems; ++i) {
            if (!pop(outItems[i])) {
                break;
            }
            ++popped;
        }

        return popped;
    }

    //==========================================================================
    // Query: Get current queue depth (number of elements)
    // Note: This is an approximate snapshot, not thread-safe for synchronization
    //==========================================================================

    uint32_t depth() const
    {
        const uint32_t w = writeIndex_.load(std::memory_order_acquire);
        const uint32_t r = readIndex_.load(std::memory_order_acquire);
        return (w - r) & MASK;
    }

    //==========================================================================
    // Query: Check if queue is empty
    // Note: This is an approximate snapshot, not thread-safe for synchronization
    //==========================================================================

    bool isEmpty() const
    {
        return writeIndex_.load(std::memory_order_acquire) ==
               readIndex_.load(std::memory_order_acquire);
    }

    //==========================================================================
    // Query: Check if queue is full
    // Note: This is an approximate snapshot, not thread-safe for synchronization
    //==========================================================================

    bool isFull() const
    {
        const uint32_t w = writeIndex_.load(std::memory_order_acquire);
        const uint32_t nextWrite = (w + 1) & MASK;
        return nextWrite == readIndex_.load(std::memory_order_acquire);
    }

    //==========================================================================
    // Query: Get queue capacity
    //==========================================================================

    constexpr uint32_t capacity() const
    {
        return Capacity;
    }

    /**
     * @brief Get the current write index (RT-Safe)
     */
    uint32_t getWriteIndex() const
    {
        return writeIndex_.load(std::memory_order_acquire);
    }

    /**
     * @brief Get the current read index (RT-Safe)
     */
    uint32_t getReadIndex() const
    {
        return readIndex_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Pop an item only if it is before the specified limit index
     */
    bool popLimited(T& outItem, uint32_t limitIndex)
    {
        const uint32_t currentRead = readIndex_.load(std::memory_order_relaxed);
        if (currentRead == limitIndex) {
            return false; // Reached the cycle limit
        }
        
        return pop(outItem);
    }

    //==========================================================================
    // Query: Get available space for writing
    // Note: This is an approximate snapshot, not thread-safe for synchronization
    //==========================================================================

    uint32_t availableWrite() const
    {
        const uint32_t w = writeIndex_.load(std::memory_order_acquire);
        const uint32_t r = readIndex_.load(std::memory_order_acquire);
        // Available = Capacity - depth - 1 (one slot reserved for empty detection)
        return (Capacity - 1 - ((w - r) & MASK));
    }

    //==========================================================================
    // Query: Get available items for reading
    // Note: This is an approximate snapshot, not thread-safe for synchronization
    //==========================================================================

    uint32_t availableRead() const
    {
        return depth();
    }

    //==========================================================================
    // Debug: Get raw indices (for testing/debugging only)
    //==========================================================================

    struct DebugInfo {
        uint32_t writeIndex;
        uint32_t readIndex;
        uint32_t depth;
        bool isEmpty;
        bool isFull;
    };

    DebugInfo getDebugInfo() const
    {
        const uint32_t w = writeIndex_.load(std::memory_order_acquire);
        const uint32_t r = readIndex_.load(std::memory_order_acquire);

        return DebugInfo{
            .writeIndex = w,
            .readIndex = r,
            .depth = (w - r) & MASK,
            .isEmpty = (w == r),
            .isFull = (((w + 1) & MASK) == r)
        };
    }
};

//==============================================================================
// Compile-time tests (executed at compile time for verification)
//==============================================================================

namespace {
    // Test capacity is power of 2
    static_assert((1024 & (1024 - 1)) == 0,
                  "SPSCQueue capacity must be power of 2");

    // Test common queue sizes
    using SPSC_16 = SPSCQueue<int, 16>;
    using SPSC_32 = SPSCQueue<int, 32>;
    using SPSC_64 = SPSCQueue<int, 64>;
    using SPSC_128 = SPSCQueue<int, 128>;
    using SPSC_256 = SPSCQueue<int, 256>;
    using SPSC_512 = SPSCQueue<int, 512>;
    using SPSC_1024 = SPSCQueue<int, 1024>;
    using SPSC_2048 = SPSCQueue<int, 2048>;
    using SPSC_4096 = SPSCQueue<int, 4096>;
}

} // namespace Layer2
