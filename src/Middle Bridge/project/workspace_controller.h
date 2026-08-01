// src/Middle Bridge/workspace_controller.h
#pragma once

#include "project/iworkspace_controller.h"

namespace bridge {

/**
 * @brief Concrete implementation of IWorkspaceController.
 *
 * Stores window visibility state in a fixed-size array indexed by
 * WorkspaceWindow enum. No lower-layer dependencies.
 */
class WorkspaceController : public IWorkspaceController {
public:
    WorkspaceController() = default;
    ~WorkspaceController() override = default;

    void setWindowVisible(WorkspaceWindow window, bool visible) override;
    bool isWindowVisible(WorkspaceWindow window) const override;
    void toggleWindowVisibility(WorkspaceWindow window) override;
    void bringWindowToFront(WorkspaceWindow window) override;

private:
    static constexpr uint8_t kWindowCount = 5;
    bool visibility_[kWindowCount] = {false, false, false, false, false};
    WorkspaceWindow frontWindow_ = WorkspaceWindow::Arrangement;
};

} // namespace bridge
