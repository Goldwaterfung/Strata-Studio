#pragma once
#include "icommand_history.h"
#include <vector>
#include <variant>
#include <functional>
#include <array>
#include "musical_composition/command_history/delta_primitives.h"

namespace composition {

class CommandHistoryImpl : public ICommandHistory {
public:
    using DeltaHandler = std::function<void(const ProjectDelta&, bool isUndo)>;
    using HistoryEntry = std::variant<ProjectDelta, CompoundDelta>;

    void pushDelta(const ProjectDelta& delta) override;
    bool undo() override;
    bool redo() override;
    void clear() override;
    bool canUndo() const override;
    bool canRedo() const override;
    bool isDirty() const override;
    void beginCompound() override;
    void endCompound() override;
    void setOnHistoryChangedCallback(OnHistoryChangedCallback cb) override;

    /**
     * @brief Register a subsystem to handle its own deltas.
     */
    void registerHandler(SubsystemID id, DeltaHandler handler);

private:
    /// Dispatches a single ProjectDelta through the registered handler.
    void dispatchDelta(const ProjectDelta& delta, bool isUndo);

    OnHistoryChangedCallback onHistoryChanged_;

    std::vector<HistoryEntry> undoStack_;
    std::vector<HistoryEntry> redoStack_;
    std::array<DeltaHandler, static_cast<size_t>(SubsystemID::COUNT)> handlers_;
    
    bool isProcessing_ = false; // Prevent recursion if handlers push deltas
    bool inCompound_   = false; // True between beginCompound() / endCompound()
    CompoundDelta pendingCompound_;  // Accumulates steps during an open compound
};

} // namespace composition
