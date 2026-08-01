#pragma once

#include "istate_manager.h"
#include "system_primitives.h"
#include <stack>
#include <mutex>
#include <cstring>

/**
 * @file undo_stack.h
 * @brief Undo/redo stack for state navigation
 *
 * Thread-safety:
 * - All public methods are non-RT-safe (mutex protected)
 * - Designed for main/worker thread usage only
 *
 * The undo stack maintains two stacks:
 * - Undo stack: Actions that can be undone
 * - Redo stack: Actions that can be redone
 *
 * When a new action is performed, the redo stack is cleared.
 * When undo is performed, the action moves to the redo stack.
 * When redo is performed, the action moves back to the undo stack.
 */

namespace Layer2 {

/**
 * @brief Stack-based undo/redo management
 *
 * Stores state deltas for navigation through state history.
 * Each entry contains the delta and the resulting state snapshot.
 */
class UndoStack {
private:
    /**
     * @brief Single entry in the undo/redo history
     */
    struct StackEntry {
        StateDelta delta;              ///< Delta that was applied
        StateSnapshotID resultingState; ///< State after applying delta

        StackEntry() {
            delta.previousId = StateSnapshotID{};
            delta.newId = StateSnapshotID{};
            delta.deltaDataId = Layer2::IStateManager::INVALID_DELTA_ID;
            delta.description[0] = '\0';
            delta.timestamp = 0;
            delta.checksum = 0;
        }
    };

    std::stack<StackEntry> undoStack;
    std::stack<StackEntry> redoStack;
    mutable std::mutex mutex;

public:
    /**
     * @brief Construct undo stack
     */
    UndoStack() = default;

    ~UndoStack() = default;

    // Disable copy/move
    UndoStack(const UndoStack&) = delete;
    UndoStack& operator=(const UndoStack&) = delete;

    /**
     * @brief Push a new action onto the undo stack
     *
     * Clears the redo stack (new action invalidates redo history).
     *
     * @param delta State delta that was applied
     * @param resultingState State snapshot after applying delta
     */
    void push(const StateDelta& delta, StateSnapshotID resultingState) {
        std::lock_guard<std::mutex> lock(mutex);

        // Clear redo stack - new action invalidates redo history
        while (!redoStack.empty()) {
            redoStack.pop();
        }

        // Push to undo stack
        StackEntry entry;
        entry.delta = delta;
        entry.resultingState = resultingState;

        undoStack.push(std::move(entry));
    }

    /**
     * @brief Undo the last action
     *
     * Pops from undo stack and pushes to redo stack.
     *
     * @param outDelta Output: delta that was undone
     * @param outStateId Output: state to restore to
     * @return true if undo succeeded, false if nothing to undo
     */
    bool undo(StateDelta& outDelta, StateSnapshotID& outStateId) {
        std::lock_guard<std::mutex> lock(mutex);

        if (undoStack.empty()) {
            return false;
        }

        // Pop from undo stack
        StackEntry entry = undoStack.top();
        undoStack.pop();

        // Copy outputs
        outDelta = entry.delta;
        outStateId = entry.delta.previousId; // Return to previous state

        // Push to redo stack
        redoStack.push(std::move(entry));

        return true;
    }

    /**
     * @brief Redo the last undone action
     *
     * Pops from redo stack and pushes to undo stack.
     *
     * @param outDelta Output: delta to reapply
     * @param outStateId Output: state after applying delta
     * @return true if redo succeeded, false if nothing to redo
     */
    bool redo(StateDelta& outDelta, StateSnapshotID& outStateId) {
        std::lock_guard<std::mutex> lock(mutex);

        if (redoStack.empty()) {
            return false;
        }

        // Pop from redo stack
        StackEntry entry = redoStack.top();
        redoStack.pop();

        // Copy outputs
        outDelta = entry.delta;
        outStateId = entry.resultingState;

        // Push to undo stack
        undoStack.push(std::move(entry));

        return true;
    }

    /**
     * @brief Get current undo stack depth
     *
     * @return Number of undoable actions
     */
    uint32_t getUndoDepth() const {
        std::lock_guard<std::mutex> lock(mutex);
        return static_cast<uint32_t>(undoStack.size());
    }

    /**
     * @brief Get current redo stack depth
     *
     * @return Number of redoable actions
     */
    uint32_t getRedoDepth() const {
        std::lock_guard<std::mutex> lock(mutex);
        return static_cast<uint32_t>(redoStack.size());
    }

    /**
     * @brief Check if undo is available
     *
     * @return true if undo stack has entries
     */
    bool canUndo() const {
        std::lock_guard<std::mutex> lock(mutex);
        return !undoStack.empty();
    }

    /**
     * @brief Check if redo is available
     *
     * @return true if redo stack has entries
     */
    bool canRedo() const {
        std::lock_guard<std::mutex> lock(mutex);
        return !redoStack.empty();
    }

    /**
     * @brief Clear both stacks
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        while (!undoStack.empty()) {
            undoStack.pop();
        }
        while (!redoStack.empty()) {
            redoStack.pop();
        }
    }
};

} // namespace Layer2
