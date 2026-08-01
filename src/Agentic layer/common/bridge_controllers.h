#pragma once

namespace bridge {
class ITrackController;
class ITimelineController;
class IBrowserController;
} // namespace bridge

namespace agentic {

struct BridgeControllers {
    bridge::ITrackController* trackController{nullptr};
    bridge::ITimelineController* timelineController{nullptr};
    bridge::IBrowserController* browserController{nullptr};
};

} // namespace agentic
