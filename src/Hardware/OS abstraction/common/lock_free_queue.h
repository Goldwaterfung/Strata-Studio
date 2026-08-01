// lock_free_queue.h
// Layer 1: Hardware/OS Abstraction - Lock-Free SPSC Queue
// Single Producer, Single Consumer ring buffer (wait-free)

#pragma once

#include <cstdint>
#include <atomic>
#include <algorithm>

namespace Layer1 {

// =============================================================================
// LOCK-FREE SPSC QUEUE
// =============================================================================

template<typename T, uint32_t Capacity>
class LockFreeSPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of 2 for efficient wraparound");

public:
    LockFreeSPSCQueue() : readIndex(0), writeIndex(0) {
        // Capacity is fixed at compile-time, no allocation needed
    }

    ~LockFreeSPSCQueue() = default;

    // Producer: Add item to queue (wait-free)
    // Returns: true if item was added, false if queue is full
    bool push(const T& item) {
        const uint32_t currentWrite = writeIndex.load(std::memory_order_relaxed);
        const uint32_t nextWrite = (currentWrite + 1) & mask;

        // Check if queue is full
        if (nextWrite == readIndex.load(std::memory_order_acquire)) {
            return false;  // Queue full
        }

        buffer[currentWrite] = item;

        // Publish the item
        writeIndex.store(nextWrite, std::memory_order_release);
        return true;
    }

    // Consumer: Remove item from queue (wait-free)
    // Returns: true if item was retrieved, false if queue is empty
    bool pop(T& outItem) {
        const uint32_t currentRead = readIndex.load(std::memory_order_relaxed);

        // Check if queue is empty
        if (currentRead == writeIndex.load(std::memory_order_acquire)) {
            return false;  // Queue empty
        }

        outItem = buffer[currentRead];

        // Update read index
        readIndex.store((currentRead + 1) & mask, std::memory_order_release);
        return true;
    }

    // Query operations (non-locking, may race)
    bool isEmpty() const {
        return readIndex.load(std::memory_order_acquire) ==
               writeIndex.load(std::memory_order_acquire);
    }

    bool isFull() const {
        const uint32_t nextWrite = (writeIndex.load(std::memory_order_relaxed) + 1) & mask;
        return nextWrite == readIndex.load(std::memory_order_acquire);
    }

    uint32_t size() const {
        const uint32_t w = writeIndex.load(std::memory_order_acquire);
        const uint32_t r = readIndex.load(std::memory_order_acquire);
        return (w - r) & mask;
    }

    static constexpr uint32_t capacity() { return Capacity; }

private:
    T buffer[Capacity];  // Fixed-size buffer (no allocation)
    const uint32_t mask = Capacity - 1;  // Bitmask for wraparound

    // Align to cache line to prevent false sharing
    alignas(64) std::atomic<uint32_t> readIndex;
    alignas(64) std::atomic<uint32_t> writeIndex;
};

// =============================================================================
// TYPE ERASED LOCK-FREE QUEUE INTERFACE
// =============================================================================

template<typename T>
class ILockFreeQueue {
public:
    virtual ~ILockFreeQueue() = default;
    virtual bool push(const T& item) = 0;
    virtual bool pop(T& outItem) = 0;
    virtual bool isEmpty() const = 0;
    virtual bool isFull() const = 0;
    virtual uint32_t size() const = 0;
    virtual uint32_t capacity() const = 0;
};

// Factory to create type-erased queue with runtime capacity
template<typename T>
class LockFreeQueueFactory {
public:
    static std::unique_ptr<ILockFreeQueue<T>> create(uint32_t capacity) {
        // Capacity must be power of 2, round up if needed
        uint32_t actualCapacity = 1;
        while (actualCapacity < capacity) {
            actualCapacity *= 2;
        }

        // Return appropriate specialization
        switch (actualCapacity) {
            case 64:   return std::make_unique<LockFreeQueueImpl<T, 64>>();
            case 128:  return std::make_unique<LockFreeQueueImpl<T, 128>>();
            case 256:  return std::make_unique<LockFreeQueueImpl<T, 256>>();
            case 512:  return std::make_unique<LockFreeQueueImpl<T, 512>>();
            case 1024: return std::make_unique<LockFreeQueueImpl<T, 1024>>();
            case 2048: return std::make_unique<LockFreeQueueImpl<T, 2048>>();
            case 4096: return std::make_unique<LockFreeQueueImpl<T, 4096>>();
            default:
                // For larger capacities, you'd need a dynamic implementation
                return nullptr;
        }
    }

private:
    template<typename U, uint32_t Capacity>
    class LockFreeQueueImpl : public ILockFreeQueue<U> {
    public:
        bool push(const U& item) override { return queue.push(item); }
        bool pop(U& outItem) override { return queue.pop(outItem); }
        bool isEmpty() const override { return queue.isEmpty(); }
        bool isFull() const override { return queue.isFull(); }
        uint32_t size() const override { return queue.size(); }
        uint32_t capacity() const override { return queue.capacity(); }

    private:
        LockFreeSPSCQueue<U, Capacity> queue;
    };
};

} // namespace Layer1
