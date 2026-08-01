// src/Presentation/views/playlist/TakeRowRenderer.h
#pragma once

#include <QPainter>
#include <QRectF>
#include "TrackRowRenderer.h" // For VirtualControl

namespace presentation::views {

struct TakeRowRenderer {
    static void paint(QPainter& p,
                      const QRectF& rect,
                      uint32_t takeLaneIndex,
                      uint32_t totalLanes,
                      const QRectF& promoteRect,
                      VirtualControl hoveredControl,
                      VirtualControl pressedControl);

    static VirtualControl hitTest(const QPointF& localPos,
                                  const QRectF& rect,
                                  const QRectF& promoteRect);
};

} // namespace presentation::views
