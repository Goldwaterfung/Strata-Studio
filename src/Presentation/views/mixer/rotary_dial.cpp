// src/Presentation/views/rotary_dial.cpp
#include "rotary_dial.h"
#include "../theme.h"
#include <QPainter>
#include <QPen>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace presentation::views {

RotaryDial::RotaryDial(QWidget* parent)
    : BaseTactileControl(parent) {
    // Set a premium default compact size
    setMinimumSize(40, 40);
    m_sensitivity = 0.003f; // Custom dials feel slightly heavier
}

void RotaryDial::resizeEvent(QResizeEvent* event) {
    m_bgCacheValid = false;
    BaseTactileControl::resizeEvent(event);
}

void RotaryDial::renderStaticBackground() {
    qreal dpr = devicePixelRatioF();
    
    // Initialize background cache at high physical resolution to respect Retina/4K displays
    m_bgCache = QPixmap(size() * dpr);
    m_bgCache.setDevicePixelRatio(dpr);
    m_bgCache.fill(Qt::transparent);

    QPainter painter(&m_bgCache);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPointF center(width() / 2.0, height() / 2.0);
    qreal radius = std::min(width(), height()) * 0.38;

    // 1. Draw outer anodized frame shadow
    painter.setPen(QPen(theme::Color::BgControl, 1.5));
    painter.setBrush(theme::Color::BgSurface);
    painter.drawEllipse(center, radius, radius);

    // 2. Draw subtle inner bezel ring
    painter.setPen(QPen(QColor(0, 0, 0, 80), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(center, radius - 1.5, radius - 1.5);

    // 3. Draw minimalist tick marks around the knob sweep (from 220 degrees to -40 degrees)
    // We place 5 ticks: 220 (fully counter-clockwise), 155, 90 (center), 25, -40 (fully clockwise)
    painter.setPen(QPen(theme::Color::TextMuted, 1.0));
    
    qreal startAngleDeg = 220.0;
    qreal endAngleDeg = -40.0;
    int tickCount = 5;
    
    for (int i = 0; i < tickCount; ++i) {
        qreal angleDeg = startAngleDeg - (i * (startAngleDeg - endAngleDeg) / (tickCount - 1));
        qreal angleRad = angleDeg * M_PI / 180.0;
        
        // Tick positions relative to circular bounds
        qreal innerRadius = radius + 2.5;
        qreal outerRadius = radius + 4.5;
        
        QPointF p1(center.x() + innerRadius * std::cos(angleRad),
                   center.y() - innerRadius * std::sin(angleRad));
        QPointF p2(center.x() + outerRadius * std::cos(angleRad),
                   center.y() - outerRadius * std::sin(angleRad));
                   
        painter.drawLine(p1, p2);
    }

    m_bgCacheValid = true;
}

void RotaryDial::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    // 1. Regenerate static background pixmap if size changed or cache is invalidated
    if (!m_bgCacheValid || m_bgCache.isNull()) {
        renderStaticBackground();
    }

    QPainter painter(this);
    
    // 2. Bit-blit the cached static background (Blazing fast CPU/GPU transfer)
    painter.drawPixmap(0, 0, m_bgCache);

    painter.setRenderHint(QPainter::Antialiasing, true);

    QPointF center(width() / 2.0, height() / 2.0);
    qreal radius = std::min(width(), height()) * 0.38;

    // Knob sweeping angle parameters matching ticks: 220 degrees to -40 degrees (260 degree sweep)
    qreal startAngle = 220.0;
    qreal sweepSpan = 260.0;
    
    // --- 3. Paint Active Volumetric Sweep Arc ---
    if (m_value > 0.0f) {
        QRectF arcRect(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0);
        
        // Beautiful glowing Cyber-Mint sweep pen
        QPen sweepPen(theme::Color::AccentGlow, 2.0);
        sweepPen.setCapStyle(Qt::RoundCap);
        painter.setPen(sweepPen);
        
        // Qt drawArc uses 1/16th of a degree. Positive angles sweep counter-clockwise.
        int startAngleQt = static_cast<int>(startAngle * 16.0);
        int sweepAngleQt = static_cast<int>(static_cast<double>(m_value) * -sweepSpan * 16.0); // Negative sweeps clockwise
        
        painter.drawArc(arcRect, startAngleQt, sweepAngleQt);
    }

    // --- 4. Paint Pointer Needle ---
    qreal needleAngleDeg = startAngle - (static_cast<double>(m_value) * sweepSpan);
    qreal needleAngleRad = needleAngleDeg * M_PI / 180.0;
    
    QPointF innerP(center.x() + (radius * 0.3) * std::cos(needleAngleRad),
                   center.y() - (radius * 0.3) * std::sin(needleAngleRad));
    QPointF outerP(center.x() + (radius * 0.85) * std::cos(needleAngleRad),
                   center.y() - (radius * 0.85) * std::sin(needleAngleRad));

    // Glow needle effect
    theme::PaintHelper::drawVolumetricGlow(&painter, QRectF(outerP.x() - 4.0, outerP.y() - 4.0, 8.0, 8.0), theme::Color::AccentGlow, 0.45);

    // Primary needle line drawing
    QPen needlePen(theme::Color::TextPrimary, 2.0);
    needlePen.setCapStyle(Qt::RoundCap);
    painter.setPen(needlePen);
    painter.drawLine(innerP, outerP);
}

} // namespace presentation::views
