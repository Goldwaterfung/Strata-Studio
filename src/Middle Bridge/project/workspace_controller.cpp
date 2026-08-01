// src/Middle Bridge/workspace_controller.cpp
#include "project/workspace_controller.h"

namespace bridge {

void WorkspaceController::setWindowVisible(WorkspaceWindow window, bool visible) {
    auto idx = static_cast<uint8_t>(window);
    if (idx < kWindowCount) {
        visibility_[idx] = visible;
    }
}

bool WorkspaceController::isWindowVisible(WorkspaceWindow window) const {
    auto idx = static_cast<uint8_t>(window);
    if (idx < kWindowCount) {
        return visibility_[idx];
    }
    return false;
}

void WorkspaceController::toggleWindowVisibility(WorkspaceWindow window) {
    auto idx = static_cast<uint8_t>(window);
    if (idx < kWindowCount) {
        visibility_[idx] = !visibility_[idx];
    }
}

void WorkspaceController::bringWindowToFront(WorkspaceWindow window) {
    auto idx = static_cast<uint8_t>(window);
    if (idx < kWindowCount) {
        visibility_[idx] = true;
        frontWindow_ = window;
    }
}

} // namespace bridge
