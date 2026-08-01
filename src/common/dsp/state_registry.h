#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include "../system_primitives.h"

namespace DSP {

// Global atomic counter to ensure NodeID generation uniqueness across ALL factory registries
inline std::atomic<uint32_t>& getGlobalGenerationCounter() {
    static std::atomic<uint32_t> counter{1};
    return counter;
}

/**
 * @brief Thread-safe, real-time friendly registry for DSP node states.
 * 
 * This template manages a fixed-size array of state structures (PODs)
 * and uses generation-based ABA protection to ensure handles (NodeIDs)
 * are valid even if a slot is reused.
 * 
 * @tparam StateT The POD structure representing the node's state.
 * @tparam MaxNodes The maximum number of nodes of this type allowed.
 */
template <typename StateT, uint32_t MaxNodes>
class StateRegistry {
public:
    struct Slot {
        StateT data;
        std::atomic<uint32_t> generation{0}; // 0 = free
    };

    StateRegistry() {
        // Initialize all slots to zero/free
        for (uint32_t i = 0; i < MaxNodes; ++i) {
            _slots[i].generation.store(0, std::memory_order_relaxed);
        }
    }

    /**
     * @brief Allocates a new state slot and returns a unique NodeID.
     * Called from the main/control thread.
     */
    std::optional<NodeID> allocate() {
        // Linear scan for a free slot
        for (uint32_t i = 0; i < MaxNodes; ++i) {
            uint32_t expected = 0;
            // Use the globally unique generation starting from 1
            uint32_t nextGen = getGlobalGenerationCounter().fetch_add(1, std::memory_order_relaxed);
            if (nextGen == 0) nextGen = getGlobalGenerationCounter().fetch_add(1, std::memory_order_relaxed); // Wrap around protection

            if (_slots[i].generation.compare_exchange_strong(expected, nextGen, std::memory_order_release)) {
                // Found a slot and successfully claimed it
                std::memset(&_slots[i].data, 0, sizeof(StateT));
                return NodeID{i, nextGen};
            }
        }
        return std::nullopt; // Registry full
    }

    /**
     * @brief Deallocates a state slot.
     * Called from the main/control thread.
     */
    void deallocate(NodeID nodeId) {
        if (nodeId.index() >= MaxNodes) return;
        
        uint32_t expected = nodeId.gen();
        // Setting generation to 0 marks it as free
        _slots[nodeId.index()].generation.compare_exchange_strong(expected, 0, std::memory_order_release);
    }

    /**
     * @brief High-speed validation and lookup for the audio thread.
     * O(1) performance with lock-free safety.
     * 
     * @param nodeId The handle to look up.
     * @return StateT* Pointer to the state if valid, nullptr otherwise.
     */
    inline StateT* get(NodeID nodeId) {
        uint32_t index = nodeId.index();
        if (index >= MaxNodes) return nullptr;

        // Atomic load of generation to compare against handle
        if (_slots[index].generation.load(std::memory_order_acquire) == nodeId.gen()) {
            return &_slots[index].data;
        }
        return nullptr;
    }

    /**
     * @brief Constant version of the lookup.
     */
    inline const StateT* get(NodeID nodeId) const {
        uint32_t index = nodeId.index();
        if (index >= MaxNodes) return nullptr;

        if (_slots[index].generation.load(std::memory_order_acquire) == nodeId.gen()) {
            return &_slots[index].data;
        }
        return nullptr;
    }

private:
    Slot _slots[MaxNodes];
};

} // namespace DSP
