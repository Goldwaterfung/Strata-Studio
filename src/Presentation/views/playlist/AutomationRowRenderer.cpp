// src/Presentation/views/playlist/AutomationRowRenderer.cpp
#include "AutomationRowRenderer.h"
#include "../theme.h"

namespace presentation::views {

void AutomationRowRenderer::paint(QPainter& p,
                                  const QRectF& rect,
                                  const QString& paramName,
                                  uint8_t recordMode,
                                  uint32_t trackColorARGB,
                                  VirtualControl hoveredControl,
                                  VirtualControl pressedControl)
{
    const double w = rect.width();
    const double h = rect.height();
    const double rx = rect.x();
    const double ry = rect.y();
    (void)hoveredControl;
    (void)pressedControl;

    // 1. Draw base background (slightly darker than main track)
    QColor bgColor = theme::Color::BgSurface.darker(105);
    p.fillRect(rect, bgColor);

    // Bottom border
    QColor borderColor = theme::Color::BgControl;
    borderColor.setAlpha(200);
    p.setPen(QPen(borderColor, 1));
    p.drawLine(QPointF(rx, ry + h - 1.0), QPointF(rx + w, ry + h - 1.0));

    // Left color strip (same as parent track color)
    QColor trackColor = QColor::fromRgba(trackColorARGB);
    p.fillRect(QRectF(rx, ry, 4.0, h), trackColor);

    // 2. Draw parameter name
    p.setPen(theme::Color::TextMuted);
    p.setFont(theme::Font::primary(8, QFont::Normal));
    p.drawText(QRectF(rx + 16.0, ry, w - 80.0, h),
               Qt::AlignVCenter | Qt::AlignLeft,
               paramName);

    // 3. Draw record mode status indicator
    p.setFont(theme::Font::monospace(6, QFont::Bold));
    QString modeStr;
    switch (recordMode) {
        case 1: modeStr = "READ";  p.setPen(QColor("#00FFCC")); break;
        case 2: modeStr = "TOUCH"; p.setPen(QColor("#FFCC00")); break;
        case 3: modeStr = "LATCH"; p.setPen(QColor("#FF9900")); break;
        case 4: modeStr = "WRITE"; p.setPen(QColor("#FF3333")); break;
        case 5: modeStr = "TRIM";  p.setPen(QColor("#FF00FF")); break;
        default: modeStr = "OFF";  p.setPen(theme::Color::TextMuted); break;
    }
    p.drawText(QRectF(rx + w - 70.0, ry, 60.0, h),
               Qt::AlignVCenter | Qt::AlignRight, modeStr);
}

VirtualControl AutomationRowRenderer::hitTest(const QPointF& localPos,
                                              const QRectF& rect)
{
    // Check bottom border resize hit zone
    QRectF bottomBorder(rect.x(), rect.bottom() - 4.0, rect.width(), 4.0);
    if (bottomBorder.contains(localPos)) return VirtualControl::BottomBorder;

    return VirtualControl::None;
}

} // namespace presentation::views
