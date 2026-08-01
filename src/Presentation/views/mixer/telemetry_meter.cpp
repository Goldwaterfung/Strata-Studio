// src/Presentation/views/telemetry_meter.cpp
#include "telemetry_meter.h"
#include "../theme.h"
#include <QPainter>
#include <QPen>
#include <QLinearGradient>
#include <algorithm>

namespace presentation::views {

TelemetryMeter::TelemetryMeter(QWidget* parent)
    : QWidget(parent) {
    // Set a premium vertical level strip minimum size
    setMinimumSize(44, 120);
}

void TelemetryMeter::setMeteringProvider(bridge::IMeteringProvider* provider, TrackID trackId, bool isMaster) {
    m_provider = provider;
    m_trackId = trackId;
    m_isMaster = isMaster;
}

void TelemetryMeter::setLevels(float peakLeft, float peakRight, float rmsLeft, float rmsRight, bool clipLeft, bool clipRight, qint64 nowMs) {
    bool changed = false;

    // Detect if primary visual signals changed
    if (m_peakLeft != peakLeft || m_peakRight != peakRight ||
        m_rmsLeft != rmsLeft || m_rmsRight != rmsRight) {
        changed = true;
    }

    m_peakLeft = peakLeft;
    m_peakRight = peakRight;
    m_rmsLeft = rmsLeft;
    m_rmsRight = rmsRight;

    qint64 now = (nowMs > 0) ? nowMs : QDateTime::currentMSecsSinceEpoch();

    // --- Left Channel Peak Hold Mechanics ---
    if (m_peakLeft > m_holdPeakLeft) {
        m_holdPeakLeft = m_peakLeft;
        m_holdTimeLeft = now;
        changed = true;
    } else if (now - m_holdTimeLeft > 1000) {
        // Sticky hold for 1.0s, then apply dynamic decay (-0.4 dB per paint/refill cycle)
        float newHold = std::max(m_peakLeft, m_holdPeakLeft - 0.4f);
        if (m_holdPeakLeft != newHold) {
            m_holdPeakLeft = newHold;
            changed = true;
        }
    }

    // --- Right Channel Peak Hold Mechanics ---
    if (m_peakRight > m_holdPeakRight) {
        m_holdPeakRight = m_peakRight;
        m_holdTimeRight = now;
        changed = true;
    } else if (now - m_holdTimeRight > 1000) {
        float newHold = std::max(m_peakRight, m_holdPeakRight - 0.4f);
        if (m_holdPeakRight != newHold) {
            m_holdPeakRight = newHold;
            changed = true;
        }
    }

    // Sticky clip state until manual clear click
    if (clipLeft && !m_clipLeft) {
        m_clipLeft = true;
        changed = true;
    }
    if (clipRight && !m_clipRight) {
        m_clipRight = true;
        changed = true;
    }

    // Only redraw the meter if visual bounds actually updated
    if (changed) {
        update();
    }
}

void TelemetryMeter::updatePeaks(float peakLeftdB, float peakRightdB, qint64 nowMs) {
    // Approximate RMS as 3 dB below peak; set clip when peak reaches 0 dB.
    float rmsL  = peakLeftdB  - 3.0f;
    float rmsR  = peakRightdB - 3.0f;
    bool  clipL = (peakLeftdB  >= 0.0f);
    bool  clipR = (peakRightdB >= 0.0f);
    setLevels(peakLeftdB, peakRightdB, rmsL, rmsR, clipL, clipR, nowMs);
}

double TelemetryMeter::dbToRatio(double db) const {
    // Standard DAW visualization range: -60.0 dB to +6.0 dB
    if (db <= -60.0) return 0.0;
    if (db >= 6.0) return 1.0;
    return (db - (-60.0)) / (6.0 - (-60.0));
}

void TelemetryMeter::resizeEvent(QResizeEvent* event) {
    m_bgCacheValid = false;
    QWidget::resizeEvent(event);
}

void TelemetryMeter::renderStaticBackground() {
    if (width() <= 0 || height() <= 0) {
        return;
    }
    qreal dpr = devicePixelRatioF();

    // Create high-resolution static grid cache
    m_bgCache = QPixmap(size() * dpr);
    m_bgCache.setDevicePixelRatio(dpr);
    m_bgCache.fill(Qt::transparent);

    QPainter painter(&m_bgCache);
    painter.setRenderHint(QPainter::Antialiasing, true);

    double topMargin = 12.0;
    double bottomMargin = 4.0;
    double meterHeight = height() - topMargin - bottomMargin;

    double outGutter = 2.0;
    double centerGutter = 22.0;
    double colW = (width() - (outGutter * 2.0) - centerGutter) / 2.0;
    if (colW < 4.0) {
        colW = 4.0;
    }
    double rightX = outGutter + colW + centerGutter;

    // 1. Draw channel backing slots (deep black hardware cutouts)
    painter.setPen(QPen(theme::Color::BgControl, 1.0));
    painter.setBrush(theme::Color::BgBase);
    
    painter.drawRect(QRectF(outGutter, topMargin, colW, meterHeight));
    painter.drawRect(QRectF(rightX, topMargin, colW, meterHeight));

    // 2. Draw Tapered Geometric Decibel Ladder with numeric values
    // Grid marks at: -45 dB, -30 dB, -20 dB, -12 dB, -6 dB, 0 dB (Unity), and +6 dB (Top)
    double ladderDb[] = {-45.0, -30.0, -20.0, -12.0, -6.0, 0.0, 6.0};
    int ladderSize = 7;

    painter.setFont(theme::Font::monospace(8, QFont::Bold));

    for (int i = 0; i < ladderSize; ++i) {
        double ratio = dbToRatio(ladderDb[i]);
        double y = topMargin + (1.0 - ratio) * meterHeight;

        // Determine color based on importance
        QColor lineColor = theme::Color::TextMuted;
        QColor textColor = theme::Color::TextMuted;
        if (ladderDb[i] == 0.0) {
            lineColor = theme::Color::SafetyAmber;
            textColor = theme::Color::SafetyAmber;
        }

        // Draw structural lines crossing the meter
        if (ladderDb[i] == 0.0) {
            painter.setPen(QPen(lineColor, 2.0)); // Thicker Amber unity line
            painter.drawLine(QPointF(1.0, y), QPointF(width() - 1.0, y));
        } else if (ladderDb[i] == -12.0 || ladderDb[i] == -30.0) {
            painter.setPen(QPen(lineColor, 1.0));
            painter.drawLine(QPointF(outGutter, y), QPointF(width() - outGutter, y));
        } else {
            QColor dimColor = lineColor;
            dimColor.setAlphaF(static_cast<float>(dimColor.alphaF()) * 0.6f);
            painter.setPen(QPen(dimColor, 1.0));
            painter.drawLine(QPointF(outGutter + colW, y), QPointF(rightX, y));
        }

        // Draw Numeric dB value in the central gutter
        QString numStr = (ladderDb[i] > 0.0) ? QString("+%1").arg(ladderDb[i]) : QString::number(ladderDb[i]);
        if (ladderDb[i] == 0.0) numStr = "0"; // keep it clean

        painter.setPen(textColor);
        QRectF textRect(outGutter + colW, y - 6.0, centerGutter, 12.0);
        painter.drawText(textRect, Qt::AlignCenter, numStr);
    }

    m_bgCacheValid = true;
}

void TelemetryMeter::mousePressEvent(QMouseEvent* event) {
    // Reset clipping state on clicking the upper indicator area
    if (event->position().y() < 12.0) {
        m_clipLeft = false;
        m_clipRight = false;
        
        if (m_provider) {
            if (m_isMaster) {
                m_provider->resetMasterClip();
            } else {
                m_provider->resetTrackClip(m_trackId);
            }
        }
        
        update();
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}

void TelemetryMeter::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    if (width() <= 0 || height() <= 0) {
        return;
    }

    if (!m_bgCacheValid || m_bgCache.isNull()) {
        renderStaticBackground();
    }

    if (m_bgCache.isNull()) {
        return;
    }

    QPainter painter(this);
    
    // 1. Blit backing grid
    painter.drawPixmap(0, 0, m_bgCache);

    painter.setRenderHint(QPainter::Antialiasing, true);

    double topMargin = 12.0;
    double bottomMargin = 4.0;
    double meterHeight = height() - topMargin - bottomMargin;

    double outGutter = 2.0;
    double centerGutter = 22.0;
    double colW = (width() - (outGutter * 2.0) - centerGutter) / 2.0;
    if (colW < 4.0) {
        colW = 4.0;
    }
    double rightX = outGutter + colW + centerGutter;

    // --- 2. Create Beautiful Liquid Plasma Linear Gradient ---
    QLinearGradient plasma(0.0, height() - bottomMargin, 0.0, topMargin);
    plasma.setColorAt(0.0, theme::Color::AccentGlow);    // Bottom: Cyber-Mint (#00FFCC)
    plasma.setColorAt(0.75, theme::Color::SafetyAmber);   // Unity Threshold: Amber-Gold (#FFB300)
    plasma.setColorAt(1.0, theme::Color::AccentRecord);  // Clipping Overdrive: Crimson Velvet (#FF3B30)

    // --- 3. Draw Stereo Active RMS Columns ---
    double ratioRmsL = dbToRatio(static_cast<double>(m_rmsLeft));
    double yRmsL = topMargin + (1.0 - ratioRmsL) * meterHeight;
    
    double ratioRmsR = dbToRatio(static_cast<double>(m_rmsRight));
    double yRmsR = topMargin + (1.0 - ratioRmsR) * meterHeight;

    painter.setPen(Qt::NoPen);
    painter.setBrush(plasma);

    // Left RMS filled column
    if (m_rmsLeft > -60.0f) {
        painter.drawRect(QRectF(outGutter + 0.5, yRmsL, colW - 1.0, (height() - bottomMargin) - yRmsL));
    }
    
    // Right RMS filled column
    if (m_rmsRight > -60.0f) {
        painter.drawRect(QRectF(rightX + 0.5, yRmsR, colW - 1.0, (height() - bottomMargin) - yRmsR));
    }

    // --- 4. Draw Transient Active Peak Indicators (Cyan bars) ---
    painter.setBrush(Qt::NoBrush);
    
    if (m_peakLeft > -60.0f) {
        double yPeakL = topMargin + (1.0 - dbToRatio(static_cast<double>(m_peakLeft))) * meterHeight;
        painter.setPen(QPen(theme::Color::PeakHoldWhite, 1.0));
        painter.drawLine(QPointF(outGutter + 1.0, yPeakL), QPointF(outGutter + colW - 1.0, yPeakL));
    }

    if (m_peakRight > -60.0f) {
        double yPeakR = topMargin + (1.0 - dbToRatio(static_cast<double>(m_peakRight))) * meterHeight;
        painter.setPen(QPen(theme::Color::PeakHoldWhite, 1.0));
        painter.drawLine(QPointF(rightX + 1.0, yPeakR), QPointF(rightX + colW - 1.0, yPeakR));
    }

    // --- 5. Draw Active Peak Hold Lines (Glowing Green) ---
    if (m_holdPeakLeft > -60.0f) {
        double yHoldL = topMargin + (1.0 - dbToRatio(static_cast<double>(m_holdPeakLeft))) * meterHeight;
        painter.setPen(QPen(theme::Color::AccentGlow, 1.0));
        painter.drawLine(QPointF(outGutter + 1.0, yHoldL), QPointF(outGutter + colW - 1.0, yHoldL));
    }

    if (m_holdPeakRight > -60.0f) {
        double yHoldR = topMargin + (1.0 - dbToRatio(static_cast<double>(m_holdPeakRight))) * meterHeight;
        painter.setPen(QPen(theme::Color::AccentGlow, 1.0));
        painter.drawLine(QPointF(rightX + 1.0, yHoldR), QPointF(rightX + colW - 1.0, yHoldR));
    }

    // --- 6. Draw Volumetric Clipping LED Warning Lights ---
    QRectF clipRectL(outGutter, 2.0, colW, 8.0);
    QRectF clipRectR(rightX, 2.0, colW, 8.0);

    painter.setPen(Qt::NoPen);

    // Left LED clip
    if (m_clipLeft) {
        painter.setBrush(theme::Color::AccentRecord);
        painter.drawRoundedRect(clipRectL, 1.5, 1.5);
        theme::PaintHelper::drawVolumetricGlow(&painter, QRectF(outGutter - 1.0, 1.0, colW + 2.0, 10.0), theme::Color::AccentRecord, 0.45);
    } else {
        painter.setBrush(theme::Color::ClipIndicatorOff); // Deep dim warning slot
        painter.drawRoundedRect(clipRectL, 1.5, 1.5);
    }

    // Right LED clip
    if (m_clipRight) {
        painter.setBrush(theme::Color::AccentRecord);
        painter.drawRoundedRect(clipRectR, 1.5, 1.5);
        theme::PaintHelper::drawVolumetricGlow(&painter, QRectF(rightX - 1.0, 1.0, colW + 2.0, 10.0), theme::Color::AccentRecord, 0.45);
    } else {
        painter.setBrush(theme::Color::ClipIndicatorOff);
        painter.drawRoundedRect(clipRectR, 1.5, 1.5);
    }
}

} // namespace presentation::views
