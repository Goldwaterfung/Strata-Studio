# Playlist UI Keyboard Shortcuts Reference

This document provides a comprehensive list of all global keyboard shortcuts and hotkeys implemented within the **Playlist/Arrangement UI Module** of the Antigravity DAW. 

All shortcut detection is centralized inside `PlaylistWindow::installKeyboardShortcuts()`, ensuring that no shortcut logic is scattered inside individual sub-widgets. This maintains a clean, single-entry architecture for UI actions.

---

## 1. Transport & Playback

| Shortcut | Action | Description | Connected Controller |
| :--- | :--- | :--- | :--- |
| **`Space`** | **Toggle Play/Pause** | Toggles the active playhead playback engine. | `ITimelineController` |
| **`Ctrl` + `M`** | **Add Marker** | Drops a session time marker at the current playhead frame. | `ITimelineController` |

---

## 2. Playlist Edit Tool Selector

These hotkeys allow lightning-fast switching of the active editing tool directly from the keyboard:

| Shortcut | Tool Mode | Cursor / Behavior | Description |
| :--- | :--- | :--- | :--- |
| **`P`** | **Draw Tool** | Pencil Cursor | Standard audio region placement, selection, and resizing. |
| **`B`** | **Paint Tool** | Brush Cursor | Rapid multi-clip painting and stamp-duplication of patterns. |
| **`D`** | **Delete Tool** | Eraser Cursor | Quick click-to-remove regions, curves, or clips. |
| **`T`** | **Mute Tool** | Muted Indicator | Toggles region active/mute state without deleting the clip. |
| **`S`** | **Slip Edit Tool** | Horizontal Arrows | Modifies start file offsets inside the pre-allocated region bounds. |
| **`C`** | **Slice Tool** | Razor/Cut Cursor | Splits a region into two independent blocks at the click frame. |
| **`E`** | **Select Tool** | Standard Pointer | Handles multi-region rubber-band group selection. |
| **`Shift` + `Z`** | **Zoom Select** | Magnifying Glass | Drags to zoom vertically and horizontally into a specific viewport range. |
| **`Y`** | **Play Selected** | Mini-Speaker | Auditions a clicked clip independently bypassing global solo/mute. |

---

## 3. Focus & Clip Modifiers

Quick toggle properties located on the `PlaylistClipFocusBar` below the timeline ruler:

| Shortcut | Action | UI Behavior | Description |
| :--- | :--- | :--- | :--- |
| **`Shift` + `F`** | **Toggle Show Fades** | Fade handles overlay | Shows or hides pre-allocated audio clip micro-crossfade handles. |
| **`Shift` + `M`** | **Toggle Stretch Mode** | Elastic audio drag | Toggles whether dragging a clip edge stretches/warps audio or crops it. |

---

## 4. Window & Overlay Utilities

Global panel toggles and export tools:

| Shortcut | Action | Widget Target | Description |
| :--- | :--- | :--- | :--- |
| **`Alt` + `P`** | **Toggle Picker Panel** | `PickerPanel` (Left) | Collapses or expands the left-docked MIDI Pattern & Audio clip picker. |
| **`Ctrl` + `R`** | **Offline Render / Bounce** | `RenderSettingsDialog` | Immediately invokes the modal settings for exporting the project. |
| **`Ctrl` + `F8`** | **Dashboard Overview** | `ProjectPickerScreen` | Opens the full-screen visual dashboard picker template overlay. |

---

> [!NOTE]
> All hotkeys are registered globally to the `PlaylistWindow` widget container. To prevent keyboard conflicts with standard system inputs, key signals are captured only when the main DAW or a sub-panel in the Playlist holds primary active window focus.
