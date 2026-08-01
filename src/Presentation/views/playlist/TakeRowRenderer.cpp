// src/Presentation/views/playlist/TakeRowRenderer.cpp
#include "TakeRowRenderer.h"
#include "../theme.h"

namespace presentation::views {

void TakeRowRenderer::paint(QPainter& p,
                            const QRectF& rect,
                            uint32_t takeLaneIndex,
                            uint32_t totalLanes,
                            const QRectF& promoteRect,
                            VirtualControl hoveredControl,
                            VirtualControl pressedControl)
{
    const double w = rect.width();
    const double h = rect.height();
    const double rx = rect.x();
    const double ry = rect.y();

    // 1. Draw base dark background
    QColor bgColor = theme::Color::BgSurface.darker(105);
    p.fillRect(rect, bgColor);

    // Bottom border
    QColor borderColor = theme::Color::BgControl;
    borderColor.setAlpha(200);
    p.setPen(QPen(borderColor, 1));
    p.drawLine(QPointF(rx, ry + h - 1.0), QPointF(rx + w, ry + h - 1.0));

    // 2. Draw "Take X" chronological label text
    p.setPen(theme::Color::TextMuted);
    p.setFont(theme::Font::primary(11));
    uint32_t takeNumber = (totalLanes > takeLaneIndex) ? (totalLanes - takeLaneIndex) : 1;
    QString label = QString("Take %1").arg(takeNumber);

    const int indent = 24;
    QRectF textRect = rect.adjusted(indent + 8, 0, -32, 0);
    p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, label);

    // 3. Draw Promote Button
    if (promoteRect.isValid()) {
        bool isPressed = (pressedControl == VirtualControl::PromoteButton && hoveredControl == VirtualControl::PromoteButton);
        bool isHovered = (hoveredControl == VirtualControl::PromoteButton);
        
        QColor btnBg = isPressed ? QColor("#2A2A2C") : (isHovered ? QColor("#4A4A4C") : QColor("#3A3A3C"));
        p.setPen(QPen(QColor("#2C2C2E"), 1.0));
        p.setBrush(btnBg);
        p.drawRoundedRect(promoteRect, 3.0, 3.0);
        
        p.setPen(QColor("#E0E0E0"));
        p.setFont(theme::Font::primary(9));
        p.drawText(promoteRect, Qt::AlignCenter, QString::fromUtf8("↑"));
    }
}

VirtualControl TakeRowRenderer::hitTest(const QPointF& localPos,
                                        const QRectF& rect,
                                        const QRectF& promoteRect)
{
    if (promoteRect.contains(localPos)) {
        return VirtualControl::PromoteButton;
    }
    
    // Check bottom border resize hit zone
    QRectF bottomBorder(rect.x(), rect.bottom() - 4.0, rect.width(), 4.0);
    if (bottomBorder.contains(localPos)) return VirtualControl::BottomBorder;

    return VirtualControl::None;
}

} // namespace presentation::views
