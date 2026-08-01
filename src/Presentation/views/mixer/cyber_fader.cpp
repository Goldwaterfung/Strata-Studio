// src/Presentation/views/cyber_fader.cpp
#include "cyber_fader.h"
#include "../theme.h"
#include <QPainter>
#include <QPen>
#include <algorithm>
#include "common/math/gain.h"

namespace presentation::views {

CyberFader::CyberFader(QWidget* parent)
    : BaseTactileControl(parent) {
    // Set a premium vertical standard ratio size
    setMinimumSize(44, 120);
    m_sensitivity = 0.002f; // Faders allow slightly more precise long dragging
}

void CyberFader::resizeEvent(QResizeEvent* event) {
    m_bgCacheValid = false;
    BaseTactileControl::resizeEvent(event);
}

void CyberFader::renderStaticBackground() {
    qreal dpr = devicePixelRatioF();

    // Initialize physical cached pixmap
    m_bgCache = QPixmap(size() * dpr);
    m_bgCache.setDevicePixelRatio(dpr);
    m_bgCache.fill(Qt::transparent);

    QPainter painter(&m_bgCache);
    painter.setRenderHint(QPainter::Antialiasing, true);

    double centerX = width() / 2.0;
    double margin = 15.0;
    double slotHeight = height() - (margin * 2.0);

    // 1. Draw recessed fader groove
    QRectF slotRect(centerX - 2.0, margin, 4.0, slotHeight);
    painter.setPen(QPen(theme::Color::BgControl, 1.0));
    painter.setBrush(theme::Color::BgBase);
    painter.drawRoundedRect(slotRect, 2.0, 2.0);

    // 2. Draw logarithmic fader graduation marks on the sides of the track
    // We draw ticks at: 0% (bottom), 25%, 50% (-6dB), 70.7% (Unity/0dB), and 100% (+6dB)
    std::vector<double> ticks = {0.0, 0.25, static_cast<double>(Math::Gain::CENTER_PAN_NORMALIZED), static_cast<double>(Math::Gain::UNITY_NORMALIZED), 1.0};
    for (size_t i = 0; i < ticks.size(); ++i) {
        double ratio = ticks[i];
        double tickY = height() - margin - (ratio * slotHeight);

        painter.setPen(QPen(theme::Color::TextMuted, 1.0));

        // Highlight the unity gain line (0dB) with a wider/hotter tick mark
        if (i == 3) {
            painter.setPen(QPen(theme::Color::TextPrimary, 1.5));
            painter.drawLine(QPointF(centerX - 12.0, tickY), QPointF(centerX - 5.0, tickY));
            painter.drawLine(QPointF(centerX + 5.0, tickY), QPointF(centerX + 12.0, tickY));
        } else {
            painter.drawLine(QPointF(centerX - 9.0, tickY), QPointF(centerX - 5.0, tickY));
            painter.drawLine(QPointF(centerX + 5.0, tickY), QPointF(centerX + 9.0, tickY));
        }
    }

    m_bgCacheValid = true;
}

void CyberFader::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    // 1. Regenerate cached background if size changed
    if (!m_bgCacheValid || m_bgCache.isNull()) {
        renderStaticBackground();
    }

    QPainter painter(this);
    
    // 2. Bit-blit the cached background
    painter.drawPixmap(0, 0, m_bgCache);

    painter.setRenderHint(QPainter::Antialiasing, true);

    double centerX = width() / 2.0;
    double margin = 15.0;
    double slotHeight = height() - (margin * 2.0);

    // Calculate current vertical grip position based on the normalized parameter value
    double gripY = height() - margin - (static_cast<double>(m_value) * slotHeight);

    // --- 3. Paint Active Volumetric Groove Glow ---
    if (m_value > 0.0f) {
        QPen activePen(theme::Color::AccentGlow, 2.0);
        activePen.setCapStyle(Qt::RoundCap);
        painter.setPen(activePen);
        painter.drawLine(QPointF(centerX, height() - margin), QPointF(centerX, gripY));

        // Soft volumetric neon emission around fader active terminal
        theme::PaintHelper::drawVolumetricGlow(&painter, QRectF(centerX - 4.0, gripY - 4.0, 8.0, 8.0), theme::Color::AccentGlow, 0.35);
    }

    // --- 4. Paint Premium Anodized Fader Handle (Grip) ---
    double gripW = 32.0;
    double gripH = 18.0;
    QRectF gripRect(centerX - (gripW / 2.0), gripY - (gripH / 2.0), gripW, gripH);

    // Paint anodized grip handle via PaintHelper
    theme::PaintHelper::drawControlGrip(&painter, gripRect, theme::Color::BgControl, 3.0);

    // Active fluorescent alignment strip across the middle
    QPen stripPen(theme::Color::AccentGlow, 1.5);
    stripPen.setCapStyle(Qt::RoundCap);
    painter.setPen(stripPen);
    painter.drawLine(QPointF(centerX - (gripW / 2.0) + 4.0, gripY),
                     QPointF(centerX + (gripW / 2.0) - 4.0, gripY));
}

} // namespace presentation::views
