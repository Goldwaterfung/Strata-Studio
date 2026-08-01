// src/Presentation/views/playlist/TrackRowRenderer.h
#pragma once

#include <QPainter>
#include <QRectF>
#include <QString>
#include "tracks/itrack_controller.h" // For bridge::TrackUIState & TrackID

namespace presentation::views {

enum class VirtualControl {
    None,
    MuteButton,
    SoloButton,
    ArmButton,
    MonitorButton,
    AutomationCombo,
    AutomationExpand,
    TakesExpand,
    NameLabel,
    PromoteButton,
    BottomBorder // Resize handle
};

struct TrackRowRenderer {
    static void calculateControlRects(const QRectF& rowRect,
                                      const bridge::TrackUIState& state,
                                      int depth,
                                      QRectF& outMute, QRectF& outSolo,
                                      QRectF& outArm, QRectF& outMonitor,
                                      QRectF& outCombo,
                                      QRectF& outAutoExpand, QRectF& outTakesExpand,
                                      QRectF& outName);

    static void paint(QPainter& p,
                      const QRectF& rect,
                      const bridge::TrackUIState& state,
                      const QRectF& muteRect,
                      const QRectF& soloRect,
                      const QRectF& armRect,
                      const QRectF& monitorRect,
                      const QRectF& comboRect,
                      const QRectF& autoExpandRect,
                      const QRectF& takesExpandRect,
                      const QRectF& nameRect,
                      bool isSelected,
                      bool isGrouped,
                      int depth,
                      const QString& iconPreset,
                      float peakLeftNorm,
                      float peakRightNorm,
                      VirtualControl hoveredControl,
                      VirtualControl pressedControl);

    static VirtualControl hitTest(const QPointF& localPos,
                                  const QRectF& rect,
                                  const QRectF& muteRect,
                                  const QRectF& soloRect,
                                  const QRectF& armRect,
                                  const QRectF& monitorRect,
                                  const QRectF& comboRect,
                                  const QRectF& autoExpandRect,
                                  const QRectF& takesExpandRect,
                                  const QRectF& nameRect);
};

} // namespace presentation::views
