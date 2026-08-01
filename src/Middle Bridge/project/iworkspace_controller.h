// src/Middle Bridge/iworkspace_controller.h
#pragma once

#include <cstdint>

namespace bridge {

enum class WorkspaceWindow : uint8_t {
    Arrangement,
    PianoRoll,
    ChannelRack,
    Mixer,
    Browser
};

/**
 * @brief Controller interface for workspace window visibility management.
 *
 * Tracks which primary DAW windows are visible and which has focus.
 * The Presentation layer reads/writes through this interface to
 * coordinate window toggling from the top control panel.
 */
class IWorkspaceController {
public:
    virtual ~IWorkspaceController() = default;

    virtual void setWindowVisible(WorkspaceWindow window, bool visible) = 0;
    virtual bool isWindowVisible(WorkspaceWindow window) const = 0;
    virtual void toggleWindowVisibility(WorkspaceWindow window) = 0;
    virtual void bringWindowToFront(WorkspaceWindow window) = 0;
};

} // namespace bridge
