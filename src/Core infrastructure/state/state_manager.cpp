#include "istate_manager.h"
#include "snapshot_pool.h"
#include "delta_store.h"
#include "undo_stack.h"
#include <memory>
#include <cstring>

/**
 * @file state_manager.cpp
 * @brief State Manager Implementation
 *
 * Implementation of IStateManager interface with full undo/redo support,
 * snapshot management, and delta-based state changes.
 */

namespace Layer2 {

/**
 * @brief Internal state manager implementation
 *
 * Coordinates snapshot storage, delta storage, and undo/redo stacks.
 * Thread-safe for all non-realtime operations.
 */
class StateManagerImpl : public IStateManager {
private:
    std::unique_ptr<SnapshotPool> snapshotPool;
    std::unique_ptr<DeltaStore> deltaStore;
    std::unique_ptr<UndoStack> undoStack;
    StateSnapshotID currentState;

    /**
     * @brief Calculate checksum for delta integrity
     */
    uint32_t calculateChecksum(const StateDelta& delta) const {
        uint32_t checksum = 0;

        // Include previous ID (mix high and low 32 bits)
        checksum = checksum * 31 + static_cast<uint32_t>(delta.previousId.id);
        checksum = checksum * 31 + static_cast<uint32_t>(delta.previousId.id >> 32);

        // Include new ID (mix high and low 32 bits)
        checksum = checksum * 31 + static_cast<uint32_t>(delta.newId.id);
        checksum = checksum * 31 + static_cast<uint32_t>(delta.newId.id >> 32);

        // Include delta data ID
        checksum = checksum * 31 + static_cast<uint32_t>(delta.deltaDataId);

        // Include description
        for (size_t i = 0; i < sizeof(delta.description) && delta.description[i] != '\0'; ++i) {
            checksum = checksum * 31 + static_cast<uint8_t>(delta.description[i]);
        }

        return checksum;
    }

public:
    /**
     * @brief Construct state manager
     *
     * @param initialCapacity Initial snapshot pool capacity
     */
    StateManagerImpl(uint32_t initialCapacity)
        : snapshotPool(std::make_unique<SnapshotPool>(initialCapacity))
        , deltaStore(std::make_unique<DeltaStore>())
        , undoStack(std::make_unique<UndoStack>())
        , currentState{}
    {
        // Create initial snapshot as "empty state"
        currentState = snapshotPool->createSnapshot("Initial state", nullptr, 0);
    }

    ~StateManagerImpl() override = default;

    // ========================================================================
    // Delta Data Registration
    // ========================================================================

    DeltaId registerDeltaData(const uint8_t* data, size_t size) override {
        return deltaStore->registerData(data, size);
    }

    void unregisterDeltaData(DeltaId id) override {
        deltaStore->unregisterData(id);
    }

    const uint8_t* getDeltaData(DeltaId id, size_t& outSize) const override {
        return deltaStore->getDataPtr(id, outSize);
    }

    // ========================================================================
    // Snapshot Management
    // ========================================================================

    StateSnapshotID createSnapshot(const char* description) override {
        currentState = snapshotPool->createSnapshot(description, nullptr, 0);
        return currentState;
    }

    // ========================================================================
    // State Mutation
    // ========================================================================

    StateSnapshotID applyDelta(const StateDelta& delta, Layer2::ApplyContext context) override {
        (void)context;

        // Guard clauses
        if (!delta.previousId.isValid() || !snapshotPool->snapshotExists(delta.previousId)) {
            return StateSnapshotID{};
        }

        // Retrieve delta binary payload if registered
        size_t deltaSize = 0;
        const uint8_t* deltaData = nullptr;
        if (delta.deltaDataId != INVALID_DELTA_ID) {
            deltaData = deltaStore->getDataPtr(delta.deltaDataId, deltaSize);
        }

        // Create new snapshot reflecting delta payload
        const StateSnapshotID newStateId = snapshotPool->createSnapshot(
            delta.description,
            deltaData,
            deltaSize
        );

        if (!newStateId.isValid()) {
            return StateSnapshotID{};
        }

        undoStack->push(delta, newStateId);
        currentState = newStateId;
        return newStateId;
    }

    bool restoreSnapshot(StateSnapshotID snapshotId, Layer2::ApplyContext context) override {
        (void)context;

        if (!snapshotPool->snapshotExists(snapshotId)) {
            return false;
        }

        currentState = snapshotId;
        return true;
    }

    // ========================================================================
    // Undo/Redo Navigation
    // ========================================================================

    bool undo(StateSnapshotID* outNewStateId) override {
        StateDelta delta;
        StateSnapshotID stateId;

        if (!undoStack->undo(delta, stateId)) {
            return false; // Nothing to undo
        }

        // Restore to previous state
        if (!restoreSnapshot(stateId, Layer2::ApplyContext::MAIN_THREAD)) {
            return false;
        }

        if (outNewStateId) {
            *outNewStateId = stateId;
        }

        currentState = stateId;
        return true;
    }

    bool redo(StateSnapshotID* outNewStateId) override {
        StateDelta delta;
        StateSnapshotID stateId;

        if (!undoStack->redo(delta, stateId)) {
            return false; // Nothing to redo
        }

        // Restore to next state
        if (!restoreSnapshot(stateId, Layer2::ApplyContext::MAIN_THREAD)) {
            return false;
        }

        if (outNewStateId) {
            *outNewStateId = stateId;
        }

        currentState = stateId;
        return true;
    }

    // ========================================================================
    // Stack Query
    // ========================================================================

    uint32_t getUndoStackDepth() const override {
        return undoStack->getUndoDepth();
    }

    uint32_t getRedoStackDepth() const override {
        return undoStack->getRedoDepth();
    }

    bool canUndo() const override {
        return undoStack->canUndo();
    }

    bool canRedo() const override {
        return undoStack->canRedo();
    }

    // ========================================================================
    // Memory Management
    // ========================================================================

    void setMaxMemoryBytes(uint64_t maxBytes) override {
        snapshotPool->setMaxMemoryBytes(maxBytes);
    }

    uint64_t getCurrentMemoryBytes() const override {
        return snapshotPool->getCurrentMemoryBytes();
    }
};

// ========================================================================
// Factory Implementation
// ========================================================================

std::unique_ptr<IStateManager> IStateManager::create(uint32_t initialCapacity) {
    return std::make_unique<StateManagerImpl>(initialCapacity);
}

} // namespace Layer2
