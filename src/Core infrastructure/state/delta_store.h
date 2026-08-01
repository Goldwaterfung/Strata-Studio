#pragma once

#include "istate_manager.h"
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <cstring>

/**
 * @file delta_store.h
 * @brief Delta data storage with reference counting
 *
 * Thread-safety:
 * - All public methods are non-RT-safe (mutex protected)
 * - Designed for main/worker thread usage only
 */

namespace Layer2 {

/**
 * @brief Storage for delta data blobs with reference counting
 *
 * Delta data can be large (e.g., serialized graph changes).
 * Instead of duplicating data in each StateDelta, we store
 * it once and reference by DeltaId.
 *
 * Reference Counting:
 * - Each delta has a reference count
 * - Data is freed when count reaches zero
 * - Allows multiple StateDelta objects to share data
 *
 * Thread Safety:
 * - All operations protected by mutex
 * - NOT suitable for real-time audio thread
 */
class DeltaStore {
private:
    /**
     * @brief Internal delta data storage with reference count
     */
    struct DeltaData {
        std::vector<uint8_t> data;    ///< Delta payload
        uint32_t refCount;            ///< Reference count

        DeltaData() : refCount(0) {}
    };

    std::unordered_map<uint32_t, DeltaData> deltaData;
    mutable std::mutex mutex;
    uint32_t nextDeltaId;

    /**
     * @brief Get next valid delta ID
     *
     * Skips INVALID_DELTA_ID.
     */
    uint32_t allocateId() {
        uint32_t id = nextDeltaId;
        if (id == Layer2::IStateManager::INVALID_DELTA_ID) {
            ++id;
        }
        nextDeltaId = id + 1;
        return id;
    }

public:
    /**
     * @brief Construct delta store
     */
    DeltaStore()
        : nextDeltaId(0)
    {
        deltaData.reserve(100);
    }

    ~DeltaStore() = default;

    // Disable copy/move
    DeltaStore(const DeltaStore&) = delete;
    DeltaStore& operator=(const DeltaStore&) = delete;

    /**
     * @brief Register delta data
     *
     * Stores a copy of the data and returns an identifier.
     * Initial reference count is 1.
     *
     * @param data Pointer to delta data
     * @param size Size of data in bytes
     * @return DeltaId identifier, or INVALID_DELTA_ID on failure
     */
    Layer2::IStateManager::DeltaId registerData(const uint8_t* data, size_t size) {
        if (!data || size == 0) {
            return Layer2::IStateManager::INVALID_DELTA_ID;
        }

        std::lock_guard<std::mutex> lock(mutex);

        uint32_t id = allocateId();

        auto& entry = deltaData[id];
        entry.data.assign(data, data + size);
        entry.refCount = 1;

        return static_cast<Layer2::IStateManager::DeltaId>(id);
    }

    /**
     * @brief Unregister delta data
     *
     * Decrements reference count. Data is freed when count reaches zero.
     *
     * @param id Delta identifier to unregister
     */
    void unregisterData(Layer2::IStateManager::DeltaId id) {
        if (id == Layer2::IStateManager::INVALID_DELTA_ID) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex);

        auto it = deltaData.find(static_cast<uint32_t>(id));
        if (it == deltaData.end()) {
            return;
        }

        if (it->second.refCount > 0) {
            it->second.refCount--;
        }

        if (it->second.refCount == 0) {
            deltaData.erase(it);
        }
    }

    /**
     * @brief Retrieve delta data
     *
     * @param id Delta identifier
     * @param outData Output buffer for data
     * @param outSize Input: buffer size, Output: actual data size
     * @return true if delta found and data retrieved
     */
    bool getData(Layer2::IStateManager::DeltaId id,
                uint8_t* outData,
                size_t& outSize) const {
        if (id == Layer2::IStateManager::INVALID_DELTA_ID) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex);

        auto it = deltaData.find(static_cast<uint32_t>(id));
        if (it == deltaData.end()) {
            return false;
        }

        const auto& entry = it->second;

        // Copy data if buffer large enough
        if (outData && outSize >= entry.data.size()) {
            std::memcpy(outData, entry.data.data(), entry.data.size());
        }
        outSize = entry.data.size();

        return true;
    }

    /**
     * @brief Retrieve pointer to delta data
     *
     * @param id Delta identifier
     * @param outSize Output actual data size
     * @return Pointer to data, or nullptr if not found
     */
    const uint8_t* getDataPtr(Layer2::IStateManager::DeltaId id, size_t& outSize) const {
        if (id == Layer2::IStateManager::INVALID_DELTA_ID) {
            return nullptr;
        }

        std::lock_guard<std::mutex> lock(mutex);

        auto it = deltaData.find(static_cast<uint32_t>(id));
        if (it == deltaData.end()) {
            return nullptr;
        }

        outSize = it->second.data.size();
        return it->second.data.data();
    }

    /**
     * @brief Add reference to delta data
     *
     * Increments reference count for the specified delta.
     *
     * @param id Delta identifier
     */
    void addRef(Layer2::IStateManager::DeltaId id) {
        if (id == Layer2::IStateManager::INVALID_DELTA_ID) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex);

        auto it = deltaData.find(static_cast<uint32_t>(id));
        if (it != deltaData.end()) {
            it->second.refCount++;
        }
    }

    /**
     * @brief Release reference to delta data
     *
     * Decrements reference count. Data is freed when count reaches zero.
     * Alias for unregisterData().
     *
     * @param id Delta identifier
     */
    void release(Layer2::IStateManager::DeltaId id) {
        unregisterData(id);
    }

    /**
     * @brief Clear all delta data
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        deltaData.clear();
        nextDeltaId = 0;
    }
};

} // namespace Layer2
