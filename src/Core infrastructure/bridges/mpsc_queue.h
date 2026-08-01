// mpsc_queue.h
// Multi Producer Single Consumer Lock-Free Queue
//
// IMPLEMENTATION NOTES:
// - Wait-free push (multiple producers)
// - Wait-free pop (single consumer)
// - Based on linked-list design (not bounded, but nodes can be pooled)
// - Cache-line alignment to prevent false sharing
//
// THREAD SAFETY:
// - push(): Thread-safe from multiple producer threads
// - pop(): Single consumer only (non-reentrant)
// - Thread-safe when used by multiple producers and one consumer
//
// PERFORMANCE:
// - Target: <50ns per push/pop operation
// - Uses atomic operations with acquire/release semantics

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

namespace Layer2 {

//==============================================================================
// MPSC Queue Node - Linked List Node
//==============================================================================

template<typename T>
struct MPSCQueueNode {
    alignas(64) T data;
    std::atomic<MPSCQueueNode*> next;

    MPSCQueueNode()
        : next(nullptr)
    {}

    explicit MPSCQueueNode(const T& item)
        : data(item)
        , next(nullptr)
    {}

    template<typename U>
    explicit MPSCQueueNode(U&& item)
        : data(std::forward<U>(item))
        , next(nullptr)
    {}
};

//==============================================================================
// MPSC Queue - Multi Producer, Single Consumer Lock-Free Queue
//==============================================================================

template<typename T>
class MPSCQueue {
    // T must be trivially copyable for efficient copying
    static_assert(std::is_trivially_copyable<T>::value,
                  "MPSCQueue T must be trivially copyable");

private:
    using Node = MPSCQueueNode<T>;

    // Tail pointer (write end) - accessed by producers
    alignas(64) std::atomic<Node*> tail_;

    // Head pointer (read end) - accessed by consumer
    alignas(64) Node* head_;

    // Optional node pool for reducing allocations
    struct NodePool {
        std::atomic<Node*> freeList;
        std::atomic<uint32_t> poolSize;
        uint32_t maxPoolSize;

        NodePool(uint32_t maxSize = 256)
            : freeList(nullptr)
            , poolSize(0)
            , maxPoolSize(maxSize)
        {
            // Pre-allocate nodes to prevent runtime allocations
            Node* headNode = nullptr;
            for (uint32_t i = 0; i < maxSize; ++i) {
                Node* n = new Node();
                n->next.store(headNode, std::memory_order_relaxed);
                headNode = n;
            }
            freeList.store(headNode, std::memory_order_relaxed);
            poolSize.store(maxSize, std::memory_order_relaxed);
        }

        ~NodePool()
        {
            // Drain free list
            Node* node = freeList.load(std::memory_order_relaxed);
            while (node != nullptr) {
                Node* next = node->next.load(std::memory_order_relaxed);
                delete node;
                node = next;
            }
        }

        Node* acquire()
        {
            Node* node = freeList.load(std::memory_order_acquire);
            while (node != nullptr) {
                Node* next = node->next.load(std::memory_order_relaxed);
                if (freeList.compare_exchange_weak(node, next,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire)) {
                    poolSize.fetch_sub(1, std::memory_order_relaxed);
                    return node;
                }
            }
            return nullptr;  // Pool empty
        }

        void release(Node* node)
        {
            if (poolSize.load(std::memory_order_relaxed) >= maxPoolSize) {
                delete node;  // Pool full
                return;
            }

            node->next.store(nullptr, std::memory_order_relaxed);

            Node* currentHead = freeList.load(std::memory_order_relaxed);
            do {
                node->next.store(currentHead, std::memory_order_relaxed);
            } while (!freeList.compare_exchange_weak(currentHead, node,
                                                       std::memory_order_acq_rel,
                                                       std::memory_order_relaxed));

            poolSize.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::unique_ptr<NodePool> nodePool_;

public:
    //==========================================================================
    // Constructor
    //==========================================================================

    explicit MPSCQueue(bool enableNodePool = true, uint32_t maxPoolSize = 256)
        : nodePool_(enableNodePool ? new NodePool(maxPoolSize) : nullptr)
    {
        // Create dummy node (simplifies push/pop logic)
        Node* dummy = new Node();
        head_ = dummy;
        tail_.store(dummy, std::memory_order_relaxed);
    }

    //==========================================================================
    // Destructor
    //==========================================================================

    ~MPSCQueue()
    {
        // Drain queue and delete all nodes
        T dummyData;
        while (pop(dummyData)) {
            // Data is discarded, nodes are deleted/returned to pool by pop()
        }

        // The final head_ is the last dummy node that wasn't popped
        // We MUST delete it directly because it's never in the pool
        delete head_;
    }

    //==========================================================================
    // Disabled copy/move (queue manages exclusive ownership)
    //==========================================================================

    MPSCQueue(const MPSCQueue&) = delete;
    MPSCQueue& operator=(const MPSCQueue&) = delete;
    MPSCQueue(MPSCQueue&&) = delete;
    MPSCQueue& operator=(MPSCQueue&&) = delete;

    //==========================================================================
    // Producer: Push item to queue (wait-free, thread-safe)
    // Returns: true if successful (always true for MPSC)
    // Thread-safety: Multiple producers
    //==========================================================================

    bool push(const T& item)
    {
        // Try to get node from pool
        Node* node = nullptr;
        if (nodePool_) {
            node = nodePool_->acquire();
        }

        // Return false if pool empty or disabled (no RT heap allocations allowed)
        if (node == nullptr) {
            return false;
        }

        // Reuse pooled node
        node->data = item;
        node->next.store(nullptr, std::memory_order_relaxed);

        // Push to tail (link list)
        Node* prev = tail_.exchange(node, std::memory_order_acq_rel);
        prev->next.store(node, std::memory_order_release);

        return true;
    }

    //==========================================================================
    // Producer: Push item with move semantics
    //==========================================================================

    template<typename U = T>
    bool push(U&& item)
    {
        Node* node = nullptr;
        if (nodePool_) {
            node = nodePool_->acquire();
        }

        // Return false if pool empty or disabled (no RT heap allocations allowed)
        if (node == nullptr) {
            return false;
        }

        node->data = std::forward<U>(item);
        node->next.store(nullptr, std::memory_order_relaxed);

        Node* prev = tail_.exchange(node, std::memory_order_acq_rel);
        prev->next.store(node, std::memory_order_release);

        return true;
    }

    //==========================================================================
    // Producer: Push batch of items
    // Returns: number of items pushed (always count for MPSC)
    //==========================================================================

    uint32_t pushBatch(const T* items, uint32_t count)
    {
        for (uint32_t i = 0; i < count; ++i) {
            push(items[i]);
        }
        return count;
    }

    //==========================================================================
    // Consumer: Pop item from queue (wait-free, single consumer only)
    // Returns: true if successful, false if queue is empty
    // Thread-safety: Single consumer only
    //==========================================================================

    bool pop(T& outItem)
    {
        Node* head = head_;
        Node* next = head->next.load(std::memory_order_acquire);

        if (next == nullptr) {
            return false;  // Queue empty
        }

        // Read data
        outItem = next->data;

        // Advance head (return current head to pool or delete)
        head_ = next;

        if (nodePool_) {
            nodePool_->release(head);
        } else {
            delete head;
        }

        return true;
    }

    /**
     * @brief Get the current tail node for latching (RT-Safe)
     */
    Node* getTail() const
    {
        return tail_.load(std::memory_order_acquire);
    }

    /**
     * @brief Pop an item only if the current node is not the limit node
     */
    bool popLimited(T& outItem, Node* limitNode)
    {
        // If head_ is the limitNode, we've reached the end of the latched cycle.
        // head_ points to the LAST consumed node (dummy or otherwise).
        if (head_ == limitNode) {
            return false;
        }

        return pop(outItem);
    }

    //==========================================================================
    // Consumer: Pop multiple items (batch processing)
    // Returns: number of items popped
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
    // Consumer: Pop all available items
    // Returns: number of items popped
    //==========================================================================

    uint32_t popAll(T* outItems, uint32_t maxItems)
    {
        return popMultiple(outItems, maxItems);
    }

    //==========================================================================
    // Query: Check if queue is empty
    // Note: This is a snapshot, may change immediately after call
    //==========================================================================

    bool isEmpty() const
    {
        Node* head = head_;
        Node* next = head->next.load(std::memory_order_acquire);
        return (next == nullptr);
    }

    //==========================================================================
    // Query: Get approximate depth
    // Note: This is NOT accurate for MPSC (walks entire list)
    // Use only for debugging/monitoring
    //==========================================================================

    uint32_t approximateDepth() const
    {
        uint32_t depth = 0;
        Node* node = head_->next.load(std::memory_order_acquire);

        while (node != nullptr) {
            ++depth;
            node = node->next.load(std::memory_order_acquire);
        }

        return depth;
    }

    //==========================================================================
    // Query: Get node pool statistics
    //==========================================================================

    struct PoolStats {
        uint32_t poolSize;
        uint32_t maxPoolSize;
    };

    bool getPoolStats(PoolStats& outStats) const
    {
        if (nodePool_) {
            outStats.poolSize = nodePool_->poolSize.load(std::memory_order_relaxed);
            outStats.maxPoolSize = nodePool_->maxPoolSize;
            return true;
        }
        return false;
    }

private:
    //==========================================================================
    // Helper: Pop implementation (returns node, not data)
    //==========================================================================

    Node* popImpl()
    {
        Node* head = head_;
        Node* next = head->next.load(std::memory_order_acquire);

        if (next == nullptr) {
            return nullptr;
        }

        head_ = next;
        return head;
    }
};

//==============================================================================
// Bounded MPSC Queue Variant (uses fixed-size array for telemetry)
//==============================================================================

template<typename T, uint32_t Capacity>
class BoundedMPSCQueue {
    static_assert(std::is_trivially_copyable<T>::value,
                  "BoundedMPSCQueue T must be trivially copyable");

private:
    struct Node {
        alignas(64) T data;
        std::atomic<Node*> next;
        std::atomic<bool> inUse;

        Node() : next(nullptr), inUse(false) {}
    };

    Node nodes_[Capacity];
    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;

    // Find next free node (scan forward from tail)
    Node* findFreeNode(Node* start)
    {
        Node* node = start;
        for (uint32_t i = 0; i < Capacity; ++i) {
            bool expected = false;
            if (node->inUse.compare_exchange_strong(expected, true,
                                                      std::memory_order_acq_rel)) {
                return node;
            }
            node = &nodes_[(node - nodes_ + 1) % Capacity];
        }
        return nullptr;  // No free nodes
    }

public:
    BoundedMPSCQueue()
    {
        // Initialize all nodes as free
        for (uint32_t i = 0; i < Capacity; ++i) {
            nodes_[i].inUse.store(false, std::memory_order_relaxed);
            nodes_[i].next.store(nullptr, std::memory_order_relaxed);
        }

        // Set up initial list (all nodes in free list)
        for (uint32_t i = 0; i < Capacity - 1; ++i) {
            nodes_[i].next.store(&nodes_[i + 1], std::memory_order_relaxed);
        }

        head_.store(&nodes_[0], std::memory_order_relaxed);
        tail_.store(&nodes_[0], std::memory_order_relaxed);
        nodes_[0].inUse.store(true, std::memory_order_relaxed);
    }

    bool push(const T& item)
    {
        Node* node = findFreeNode(tail_.load(std::memory_order_acquire));
        if (node == nullptr) {
            return false;  // Queue full
        }

        node->data = item;
        node->next.store(nullptr, std::memory_order_relaxed);

        Node* prev = tail_.exchange(node, std::memory_order_acq_rel);
        prev->next.store(node, std::memory_order_release);

        return true;
    }

    bool pop(T& outItem)
    {
        Node* head = head_.load(std::memory_order_acquire);
        Node* next = head->next.load(std::memory_order_acquire);

        if (next == nullptr) {
            return false;  // Queue empty
        }

        outItem = next->data;
        head_.store(next, std::memory_order_release);

        // Mark old head as free
        head->inUse.store(false, std::memory_order_release);

        return true;
    }

    bool isEmpty() const
    {
        Node* head = head_->next.load(std::memory_order_acquire);
        return (head == nullptr);
    }

    constexpr uint32_t capacity() const { return Capacity; }
};

} // namespace Layer2
