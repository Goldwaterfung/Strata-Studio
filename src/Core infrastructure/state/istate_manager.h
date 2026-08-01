#pragma once

#include "system_primitives.h"
#include <memory>
#include <cstdint>

/**
 * @file istate_manager.h
 * @brief Layer 2: State Management - Undo/Redo with Snapshot Support
 *
 * Thread-safety:
 * - createSnapshot: Non-RT-safe (mutex protected)
 * - applyDelta: Context-dependent (RT-safe if ApplyContext::REALTIME)
 * - restoreSnapshot: Context-dependent
 * - undo/redo: Non-RT-safe
 * - query methods: RT-safe (no locks)
 */

namespace Layer2 {

/**
 * @brief Context for state changes
 *
 * Determines the threading constraints for state operations.
 */
enum class ApplyContext : uint8_t {
    REALTIME,       ///< Called from audio thread (direct, no locks)
    MAIN_THREAD,    ///< Called from main thread (via mutation queue)
    WORKER_THREAD   ///< Called from worker thread (async)
};

/**
 * @brief State Manager Interface
 *
 * Provides undo/redo functionality with snapshot management and
 * delta-based state changes. Designed to work across RT/non-RT
 * boundaries with proper context awareness.
 *
 * Memory Management:
 * - Snapshots are stored with configurable memory limits
 * - Old snapshots are automatically evicted when limit exceeded
 * - Delta data uses reference counting for efficient storage
 *
 * Thread Safety:
 * - All mutation operations are thread-safe
 * - RT-safe operations are explicitly marked
 */
class IStateManager {
public:
    /**
     * @brief Delta data identifier
     *
     * Used to reference registered delta data blobs.
     */
    using DeltaId = uint32_t;

    /**
     * @brief Invalid delta identifier
     */
    static constexpr DeltaId INVALID_DELTA_ID = UINT32_MAX;

    // ========================================================================
    // Delta Data Registration
    // ========================================================================

    /**
     * @brief Register delta data for later use
     *
     * Stores a blob of data that can be referenced by DeltaId in StateDelta.
     * Useful for large payloads that shouldn't be duplicated.
     *
     * Thread-safety: Non-RT-safe
     *
     * @param data Pointer to delta data
     * @param size Size of data in bytes
     * @return DeltaId identifier for this data, or INVALID_DELTA_ID on failure
     */
    virtual DeltaId registerDeltaData(const uint8_t* data,
                                     size_t size) = 0;

    /**
     * @brief Unregister delta data
     *
     * Decrements reference count and frees data when zero.
     *
     * Thread-safety: Non-RT-safe
     *
     * @param id Delta identifier to unregister
     */
    virtual void unregisterDeltaData(DeltaId id) = 0;
    
    /**
     * @brief Retrieve delta data
     *
     * @param id Delta identifier
     * @param outSize Output actual data size
     * @return Pointer to delta data (valid until unregistered), or nullptr
     */
    virtual const uint8_t* getDeltaData(DeltaId id, size_t& outSize) const = 0;

    // ========================================================================
    // Snapshot Management
    // ========================================================================

    /**
     * @brief Create a snapshot of current state
     *
     * Captures the current application state and returns an identifier.
     * Snapshots are stored with the description for debugging/UX.
     *
     * Thread-safety: Non-RT-safe (locks mutex)
     *
     * @param description Human-readable description (max 127 chars)
     * @return StateSnapshotID identifier for this snapshot
     */
    virtual StateSnapshotID createSnapshot(const char* description) = 0;

    // ========================================================================
    // State Mutation
    // ========================================================================

    /**
     * @brief Apply a delta to current state
     *
     * Applies a state change delta and creates a new snapshot.
     * The context determines threading constraints:
     * - REALTIME: No locks, direct application (delta must be pre-validated)
     * - MAIN_THREAD: Uses main thread synchronization
     * - WORKER_THREAD: Async application
     *
     * Thread-safety: Depends on context
     *
     * @param delta State delta to apply
     * @param context Application context (threading domain)
     * @return StateSnapshotID New state snapshot after applying delta
     */
    virtual StateSnapshotID applyDelta(const StateDelta& delta,
                                      ApplyContext context) = 0;

    /**
     * @brief Restore a previous snapshot
     *
     * Restores the application state to a previously captured snapshot.
     *
     * Thread-safety: Depends on context
     *
     * @param snapshotId Snapshot to restore
     * @param context Application context
     * @return true if restored successfully, false if snapshot not found
     */
    virtual bool restoreSnapshot(StateSnapshotID snapshotId,
                                ApplyContext context) = 0;

    // ========================================================================
    // Undo/Redo Navigation
    // ========================================================================

    /**
     * @brief Undo the last state change
     *
     * Reverts to the previous state and pushes current state to redo stack.
     *
     * Thread-safety: Non-RT-safe
     *
     * @param outNewStateId Optional output for new state ID
     * @return true if undo succeeded, false if nothing to undo
     */
    virtual bool undo(StateSnapshotID* outNewStateId = nullptr) = 0;

    /**
     * @brief Redo the last undone state change
     *
     * Re-applies a previously undone state change.
     *
     * Thread-safety: Non-RT-safe
     *
     * @param outNewStateId Optional output for new state ID
     * @return true if redo succeeded, false if nothing to redo
     */
    virtual bool redo(StateSnapshotID* outNewStateId = nullptr) = 0;

    // ========================================================================
    // Stack Query
    // ========================================================================

    /**
     * @brief Get current undo stack depth
     *
     * Thread-safety: RT-safe
     *
     * @return Number of undoable states
     */
    virtual uint32_t getUndoStackDepth() const = 0;

    /**
     * @brief Get current redo stack depth
     *
     * Thread-safety: RT-safe
     *
     * @return Number of redoable states
     */
    virtual uint32_t getRedoStackDepth() const = 0;

    /**
     * @brief Check if undo is available
     *
     * Thread-safety: RT-safe
     *
     * @return true if undo stack has entries
     */
    virtual bool canUndo() const = 0;

    /**
     * @brief Check if redo is available
     *
     * Thread-safety: RT-safe
     *
     * @return true if redo stack has entries
     */
    virtual bool canRedo() const = 0;

    // ========================================================================
    // Memory Management
    // ========================================================================

    /**
     * @brief Set maximum memory for snapshot storage
     *
     * When exceeded, oldest snapshots are evicted first.
     *
     * Thread-safety: Non-RT-safe
     *
     * @param maxBytes Maximum memory in bytes
     */
    virtual void setMaxMemoryBytes(uint64_t maxBytes) = 0;

    /**
     * @brief Get current snapshot memory usage
     *
     * Thread-safety: RT-safe
     *
     * @return Current memory usage in bytes
     */
    virtual uint64_t getCurrentMemoryBytes() const = 0;

    // ========================================================================
    // Factory
    // ========================================================================

    /**
     * @brief Create a state manager instance
     *
     * @param initialCapacity Initial number of snapshots to pre-allocate
     * @return Unique pointer to new state manager
     */
    static std::unique_ptr<IStateManager> create(uint32_t initialCapacity = 100);

    virtual ~IStateManager() = default;
};

} // namespace Layer2

// Ensure ApplyContext is POD for cross-layer passing
static_assert(std::is_pod_v<Layer2::ApplyContext>);
