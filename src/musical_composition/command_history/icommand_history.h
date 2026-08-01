#pragma once
#include "delta_primitives.h"
#include <functional>

namespace composition {

class ICommandHistory {
public:
    virtual ~ICommandHistory() = default;

    using OnHistoryChangedCallback = std::function<void()>;
    virtual void setOnHistoryChangedCallback(OnHistoryChangedCallback cb) = 0;

    virtual void pushDelta(const ProjectDelta& delta) = 0;
    virtual bool undo() = 0;
    virtual bool redo() = 0;
    virtual void clear() = 0;

    virtual bool canUndo() const = 0;
    virtual bool canRedo() const = 0;
    virtual bool isDirty() const = 0;

    /**
     * @brief Open a compound transaction. All subsequent pushDelta() calls
     *        until endCompound() are grouped into a single undo/redo step.
     * @note  Nesting is not supported. Calling beginCompound() while a
     *        compound is already open is a no-op.
     * @thread_safety NRT only.
     */
    virtual void beginCompound() = 0;

    /**
     * @brief Close the current compound transaction and commit it to the
     *        undo stack. If no deltas were pushed since beginCompound(),
     *        nothing is added to the stack.
     * @thread_safety NRT only.
     */
    virtual void endCompound() = 0;
};

} // namespace composition
