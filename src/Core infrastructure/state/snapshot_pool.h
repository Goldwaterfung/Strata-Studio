#pragma once

#include "istate_manager.h"
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <cstring>

/**
 * @file snapshot_pool.h
 * @brief Snapshot storage with memory management
 *
 * Thread-safety:
 * - All public methods are non-RT-safe (mutex protected)
 * - Designed for main/worker thread usage only
 */

namespace Layer2 {

/**
 * @brief Pool for storing state snapshots with memory limits
 *
 * Manages snapshot storage with automatic eviction when memory limits
 * are exceeded. Oldest snapshots are evicted first (FIFO).
 *
 * Memory Management:
 * - Tracks total memory usage
 * - Enforces configurable memory limit
 * - Evicts oldest snapshots when limit exceeded
 *
 * Thread Safety:
 * - All operations protected by mutex
 * - NOT suitable for real-time audio thread
 */
class SnapshotPool {
private:
    /**
     * @brief Internal snapshot data storage
     */
    struct SnapshotData {
        StateSnapshotID id;              ///< Snapshot identifier
        std::vector<uint8_t> data;       ///< Serialized state data
        char description[128];           ///< Human-readable description
        uint64_t timestamp;              ///< Creation time (nanoseconds)
        uint32_t checksum;               ///< Data integrity checksum

        SnapshotData() : timestamp(0), checksum(0) {
            description[0] = '\0';
        }
    };

    std::vector<SnapshotData> snapshots;            ///< Snapshot storage
    std::unordered_map<uint64_t, uint32_t> idToIndex; ///< ID -> vector index mapping
    mutable std::mutex mutex;                       ///< Thread safety
    uint64_t nextSnapshotId;                        ///< Next ID to allocate
    uint64_t maxMemoryBytes;                        ///< Memory limit
    uint64_t currentMemoryBytes;                    ///< Current usage

    /**
     * @brief Calculate checksum for data integrity
     */
    uint32_t calculateChecksum(const uint8_t* data, size_t size) const {
        uint32_t checksum = 0;
        for (size_t i = 0; i < size; ++i) {
            checksum = checksum * 31 + data[i];
        }
        return checksum;
    }

    /**
     * @brief Evict oldest snapshots until under memory limit
     *
     * Must be called with mutex locked.
     */
    void evictOldSnapshotsIfNeeded() {
        while (currentMemoryBytes > maxMemoryBytes && !snapshots.empty()) {
            // Remove oldest (first) snapshot
            const auto& oldest = snapshots.front();

            // Update memory tracking
            currentMemoryBytes -= oldest.data.size();

            // Remove from index map
            idToIndex.erase(oldest.id.id);

            // Shift remaining snapshots
            snapshots.erase(snapshots.begin());

            // Rebuild index map (shift indices)
            for (size_t i = 0; i < snapshots.size(); ++i) {
                idToIndex[snapshots[i].id.id] = static_cast<uint32_t>(i);
            }
        }
    }

public:
    /**
     * @brief Construct snapshot pool
     *
     * @param initialCapacity Number of snapshots to pre-allocate
     * @param maxMemoryBytes Maximum memory before eviction (default: 100MB)
     */
    SnapshotPool(uint32_t initialCapacity, uint64_t maxMemoryBytes = 100 * 1024 * 1024)
        : nextSnapshotId(1)
        , maxMemoryBytes(maxMemoryBytes)
        , currentMemoryBytes(0)
    {
        snapshots.reserve(initialCapacity);
        idToIndex.reserve(initialCapacity);
    }

    ~SnapshotPool() = default;

    // Disable copy/move
    SnapshotPool(const SnapshotPool&) = delete;
    SnapshotPool& operator=(const SnapshotPool&) = delete;

    /**
     * @brief Create a new snapshot
     *
     * @param description Human-readable description
     * @param data Serialized state data
     * @param size Size of data in bytes
     * @return StateSnapshotID New snapshot identifier
     */
    StateSnapshotID createSnapshot(const char* description,
                                  const uint8_t* data,
                                  size_t size) {
        std::lock_guard<std::mutex> lock(mutex);

        // Check if eviction needed first
        currentMemoryBytes += size;
        evictOldSnapshotsIfNeeded();

        // Allocate new snapshot
        SnapshotData snapshot;
        snapshot.id = StateSnapshotID{nextSnapshotId++, 1, 0}; // id, generation=1, reserved=0
        snapshot.data.assign(data, data + size);
        snapshot.timestamp = 0; // TODO: Get actual timestamp

        // Copy description (safely truncate if needed)
        if (description) {
            std::strncpy(snapshot.description, description, sizeof(snapshot.description) - 1);
            snapshot.description[sizeof(snapshot.description) - 1] = '\0';
        }

        // Calculate checksum
        snapshot.checksum = calculateChecksum(snapshot.data.data(), snapshot.data.size());

        // Add to storage
        uint32_t index = static_cast<uint32_t>(snapshots.size());
        snapshots.push_back(std::move(snapshot));
        idToIndex[snapshots.back().id.id] = index;

        return snapshots.back().id;
    }

    /**
     * @brief Retrieve snapshot data
     *
     * @param id Snapshot identifier
     * @param outData Output buffer for data
     * @param outSize Input: buffer size, Output: actual data size
     * @return true if snapshot found and data retrieved
     */
    bool getSnapshotData(StateSnapshotID id,
                        uint8_t* outData,
                        size_t& outSize) const {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = idToIndex.find(id.id);
        if (it == idToIndex.end()) {
            return false;
        }

        const auto& snapshot = snapshots[it->second];

        // Verify checksum
        uint32_t expectedChecksum = calculateChecksum(snapshot.data.data(), snapshot.data.size());
        if (expectedChecksum != snapshot.checksum) {
            return false; // Data corruption detected
        }

        // Copy data if buffer large enough
        if (outData && outSize >= snapshot.data.size()) {
            std::memcpy(outData, snapshot.data.data(), snapshot.data.size());
        }
        outSize = snapshot.data.size();

        return true;
    }

    /**
     * @brief Check if snapshot exists
     *
     * @param id Snapshot identifier
     * @return true if snapshot exists
     */
    bool snapshotExists(StateSnapshotID id) const {
        std::lock_guard<std::mutex> lock(mutex);
        return idToIndex.find(id.id) != idToIndex.end();
    }

    /**
     * @brief Set maximum memory limit
     *
     * May trigger eviction of old snapshots.
     *
     * @param maxBytes New memory limit in bytes
     */
    void setMaxMemoryBytes(uint64_t maxBytes) {
        std::lock_guard<std::mutex> lock(mutex);
        maxMemoryBytes = maxBytes;
        evictOldSnapshotsIfNeeded();
    }

    /**
     * @brief Get current memory usage
     *
     * @return Current memory usage in bytes
     */
    uint64_t getCurrentMemoryBytes() const {
        std::lock_guard<std::mutex> lock(mutex);
        return currentMemoryBytes;
    }

    /**
     * @brief Clear all snapshots
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        snapshots.clear();
        idToIndex.clear();
        currentMemoryBytes = 0;
    }
};

} // namespace Layer2
