#pragma once

#include "ShortcutRegistry.h"
#include <QLineEdit>
#include <QObject>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QTextEdit>
#include <QWidget>
#include <functional>
#include <memory>
#include <vector>

namespace presentation::shortcuts {

// C++20 input focus guard inspector
[[nodiscard]] inline bool isTextInputActive(const QWidget* focusWidget) noexcept {
    if (!focusWidget) return false;
    return qobject_cast<const QLineEdit*>(focusWidget) != nullptr ||
           qobject_cast<const QTextEdit*>(focusWidget) != nullptr ||
           qobject_cast<const QPlainTextEdit*>(focusWidget) != nullptr;
}

// Manager responsible for creating, binding, and life-cycle managing QShortcut instances
class ShortcutManager {
public:
    static ShortcutManager& instance();

    ShortcutManager() = default;
    ~ShortcutManager() = default;

    ShortcutManager(const ShortcutManager&) = delete;
    ShortcutManager& operator=(const ShortcutManager&) = delete;

    // Bind a ShortcutAction to a target widget and callback
    QShortcut* bind(
        QWidget* parent,
        ShortcutAction action,
        std::function<void()> callback,
        Qt::ShortcutContext context = Qt::WidgetWithChildrenShortcut
    );

    // Bind an explicit QKeySequence directly (e.g. for secondary alternate keys)
    QShortcut* bindSequence(
        QWidget* parent,
        const QKeySequence& sequence,
        std::function<void()> callback,
        Qt::ShortcutContext context = Qt::WidgetWithChildrenShortcut
    );

};

} // namespace presentation::shortcuts
