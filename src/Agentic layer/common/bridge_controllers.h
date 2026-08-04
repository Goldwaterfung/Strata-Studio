#pragma once

namespace bridge {
class ITrackController;
class ITimelineController;
class IBrowserController;
class IArrangementController;
class IMidiEditorController;
} // namespace bridge

namespace agentic {

struct BridgeControllers {
    bridge::ITrackController* trackController{nullptr};
    bridge::ITimelineController* timelineController{nullptr};
    bridge::IBrowserController* browserController{nullptr};
    bridge::IArrangementController* arrangementController{nullptr};
    bridge::IMidiEditorController* midiEditorController{nullptr};
};

} // namespace agentic
