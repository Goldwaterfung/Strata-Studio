#include "command_history_impl.h"
#include <algorithm>

namespace composition {

// ---------------------------------------------------------------------------
// Private helper
// ---------------------------------------------------------------------------

void CommandHistoryImpl::dispatchDelta(const ProjectDelta& delta, bool isUndo) {
    auto& handler = handlers_[static_cast<size_t>(delta.subsystemId)];
    if (handler) {
        handler(delta, isUndo);
    }
}

// ---------------------------------------------------------------------------
// ICommandHistory – single-delta path
// ---------------------------------------------------------------------------

void CommandHistoryImpl::pushDelta(const ProjectDelta& delta) {
    if (isProcessing_) return;

    if (inCompound_) {
        // Accumulate into the open compound; do not touch the undo stack yet.
        pendingCompound_.steps.push_back(delta);
        if (onHistoryChanged_) onHistoryChanged_();
        return;
    }

    undoStack_.push_back(delta);
    redoStack_.clear();

    // Limit history size (1000 top-level entries)
    if (undoStack_.size() > 1000) {
        undoStack_.erase(undoStack_.begin());
    }
    if (onHistoryChanged_) onHistoryChanged_();
}

// ---------------------------------------------------------------------------
// ICommandHistory – compound transaction
// ---------------------------------------------------------------------------

void CommandHistoryImpl::beginCompound() {
    if (inCompound_) return; // No nesting — idempotent
    inCompound_ = true;
    pendingCompound_.steps.clear();
}

void CommandHistoryImpl::endCompound() {
    if (!inCompound_) return;
    inCompound_ = false;

    if (pendingCompound_.steps.empty()) return; // Nothing accumulated — skip

    undoStack_.push_back(std::move(pendingCompound_));
    pendingCompound_ = {}; // Reset for next use
    redoStack_.clear();

    if (undoStack_.size() > 1000) {
        undoStack_.erase(undoStack_.begin());
    }
}

// ---------------------------------------------------------------------------
// ICommandHistory – undo / redo
// ---------------------------------------------------------------------------

bool CommandHistoryImpl::undo() {
    if (undoStack_.empty() || isProcessing_) return false;

    isProcessing_ = true;
    HistoryEntry entry = std::move(undoStack_.back());
    undoStack_.pop_back();

    std::visit([&](auto&& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, ProjectDelta>) {
            dispatchDelta(e, true);
        } else if constexpr (std::is_same_v<T, CompoundDelta>) {
            // Undo compound in reverse order
            for (auto it = e.steps.rbegin(); it != e.steps.rend(); ++it) {
                dispatchDelta(*it, true);
            }
        }
    }, entry);

    redoStack_.push_back(std::move(entry));
    isProcessing_ = false;
    if (onHistoryChanged_) onHistoryChanged_();
    return true;
}

bool CommandHistoryImpl::redo() {
    if (redoStack_.empty() || isProcessing_) return false;

    isProcessing_ = true;
    HistoryEntry entry = std::move(redoStack_.back());
    redoStack_.pop_back();

    std::visit([&](auto&& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, ProjectDelta>) {
            dispatchDelta(e, false);
        } else if constexpr (std::is_same_v<T, CompoundDelta>) {
            // Redo compound in original forward order
            for (const auto& step : e.steps) {
                dispatchDelta(step, false);
            }
        }
    }, entry);

    undoStack_.push_back(std::move(entry));
    isProcessing_ = false;
    if (onHistoryChanged_) onHistoryChanged_();
    return true;
}

// ---------------------------------------------------------------------------
// ICommandHistory – clear
// ---------------------------------------------------------------------------

void CommandHistoryImpl::clear() {
    undoStack_.clear();
    redoStack_.clear();
    // Also discard any partially-open compound
    inCompound_ = false;
    pendingCompound_.steps.clear();
    if (onHistoryChanged_) onHistoryChanged_();
}

bool CommandHistoryImpl::canUndo() const {
    return !undoStack_.empty();
}

bool CommandHistoryImpl::canRedo() const {
    return !redoStack_.empty();
}

bool CommandHistoryImpl::isDirty() const {
    return !undoStack_.empty();
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void CommandHistoryImpl::registerHandler(SubsystemID id, DeltaHandler handler) {
    handlers_[static_cast<size_t>(id)] = std::move(handler);
}

void CommandHistoryImpl::setOnHistoryChangedCallback(OnHistoryChangedCallback cb) {
    onHistoryChanged_ = std::move(cb);
}

} // namespace composition
