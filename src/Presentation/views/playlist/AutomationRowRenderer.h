// src/Presentation/views/playlist/AutomationRowRenderer.h
#pragma once

#include <QPainter>
#include <QRectF>
#include <QString>
#include "TrackRowRenderer.h" // For VirtualControl

namespace presentation::views {

struct AutomationRowRenderer {
    static void paint(QPainter& p,
                      const QRectF& rect,
                      const QString& paramName,
                      uint8_t recordMode,
                      uint32_t trackColorARGB,
                      VirtualControl hoveredControl,
                      VirtualControl pressedControl);

    static VirtualControl hitTest(const QPointF& localPos,
                                  const QRectF& rect);
};

} // namespace presentation::views
