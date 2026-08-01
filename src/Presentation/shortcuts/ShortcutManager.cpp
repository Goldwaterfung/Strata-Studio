#include "ShortcutManager.h"
#include <QApplication>

namespace presentation::shortcuts {

ShortcutManager& ShortcutManager::instance() {
    static ShortcutManager instance;
    return instance;
}

QShortcut* ShortcutManager::bind(
    QWidget* parent,
    ShortcutAction action,
    std::function<void()> callback,
    Qt::ShortcutContext context)
{
    QKeySequence keySeq = ShortcutRegistry::instance().getSequence(action);
    if (keySeq.isEmpty()) {
        return nullptr;
    }
    return bindSequence(parent, keySeq, std::move(callback), context);
}

QShortcut* ShortcutManager::bindSequence(
    QWidget* parent,
    const QKeySequence& sequence,
    std::function<void()> callback,
    Qt::ShortcutContext context)
{
    if (sequence.isEmpty() || !parent) {
        return nullptr;
    }

    auto shortcut = std::make_unique<QShortcut>(sequence, parent);
    shortcut->setContext(context);

    QShortcut* rawPtr = shortcut.release();

    QObject::connect(rawPtr, &QShortcut::activated, parent, [callback = std::move(callback), parent]() {
        QWidget* fw = parent->focusWidget();
        if (!fw) {
            fw = QApplication::focusWidget();
        }
        if (isTextInputActive(fw)) {
            return;
        }
        callback();
    });

    return rawPtr;
}

} // namespace presentation::shortcuts
