// src/Presentation/views/top_control_panel/time_display.cpp
#include "time_display.h"
#include "../theme.h"
#include "Middle Bridge/timeline/itimeline_controller.h"
#include <QPainter>
#include <QMouseEvent>
#include <QDoubleSpinBox>
#include <iostream>
#include <cmath>

namespace presentation::views {

TimeDisplay::TimeDisplay(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(160, 40);
    setCursor(Qt::PointingHandCursor);
    setToolTip("Click to toggle BBT / Absolute time");
}

void TimeDisplay::updatePosition(uint32_t bar, uint32_t beat, uint32_t tick,
                                  double seconds, double bpm)
{
    bool changed = false;

    if (bar_ != bar || beat_ != beat || tick_ != tick ||
        std::abs(seconds_ - seconds) > 0.001 || std::abs(bpm_ - bpm) > 0.001) {
        changed = true;
    }

    bar_ = bar;
    beat_ = beat;
    tick_ = tick;
    seconds_ = seconds;
    bpm_ = bpm;
    
    if (changed) {
        update();
    }
}

QString TimeDisplay::formatBBT() const
{
    return QString("%1:%2:%3")
        .arg(bar_, 4, 10, QChar('0'))
        .arg(beat_, 2, 10, QChar('0'))
        .arg(tick_, 4, 10, QChar('0'));
}

QString TimeDisplay::formatAbsolute() const
{
    int totalSeconds = static_cast<int>(seconds_);
    int minutes = totalSeconds / 60;
    int secs = totalSeconds % 60;
    int centiseconds = static_cast<int>((seconds_ - totalSeconds) * 100.0);
    return QString("%1:%2:%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'))
        .arg(centiseconds, 2, 10, QChar('0'));
}

void TimeDisplay::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Background panel
    QRectF bounds(0.0, 0.0, static_cast<double>(width()), static_cast<double>(height()));
    theme::PaintHelper::drawGlassPanel(&painter, bounds, theme::Color::BgSurface, 4.0);

    // Time value (large, accent color)
    painter.setFont(theme::Font::monospace(14, QFont::Bold));
    painter.setPen(theme::Color::AccentGlow);
    QString timeStr = showBBT_ ? formatBBT() : formatAbsolute();
    painter.drawText(bounds.adjusted(8, 0, -8, -14), Qt::AlignCenter, timeStr);

    // BPM label (small, muted)
    painter.setFont(theme::Font::monospace(8));
    painter.setPen(theme::Color::TextMuted);
    QString bpmStr = QString("%1 BPM").arg(bpm_, 0, 'f', 1);
    painter.drawText(bounds.adjusted(8, 22, -8, -2), Qt::AlignCenter, bpmStr);
}

void TimeDisplay::bind(bridge::ITimelineController* controller)
{
    m_controller = controller;
}

void TimeDisplay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (event->position().y() >= 20.0) {
            draggingBpm_ = true;
            dragStartPos_ = event->position();
            startBpm_ = bpm_;
        } else {
            showBBT_ = !showBBT_;
            update();
        }
    }
    QWidget::mousePressEvent(event);
}

void TimeDisplay::mouseMoveEvent(QMouseEvent* event)
{
    if (draggingBpm_) {
        double deltaY = dragStartPos_.y() - event->position().y();
        double sensitivity = 0.2; // BPM per pixel drag
        double newBpm = startBpm_ + deltaY * sensitivity;
        if (newBpm < 1.0) newBpm = 1.0;
        if (newBpm > 999.0) newBpm = 999.0;
        
        if (m_controller && !m_controller->isTempoAutomated()) {
            m_controller->setBPM(newBpm);
        }
        bpm_ = newBpm;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void TimeDisplay::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        draggingBpm_ = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void TimeDisplay::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && event->position().y() >= 20.0) {
        if (!m_bpmSpin) {
            m_bpmSpin = new QDoubleSpinBox(this);
            m_bpmSpin->setRange(1.0, 999.0);
            m_bpmSpin->setDecimals(1);
            m_bpmSpin->setSingleStep(0.1);
            m_bpmSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
            m_bpmSpin->setAlignment(Qt::AlignCenter);
            m_bpmSpin->setStyleSheet(QString(
                "QDoubleSpinBox { background-color: %1; border: 1px solid %2; "
                "border-radius: 4px; color: %2; font-family: 'Inter'; font-weight: bold; font-size: 12px; }"
            ).arg(theme::Color::BgSurface.name())
             .arg(theme::Color::AccentGlow.name()));
            m_bpmSpin->setGeometry(10, 5, width() - 20, height() - 10);
            connect(m_bpmSpin, &QDoubleSpinBox::editingFinished, this, &TimeDisplay::onBPMEditingFinished);
        }
        m_bpmSpin->setValue(bpm_);
        m_bpmSpin->show();
        m_bpmSpin->setFocus();
        m_bpmSpin->selectAll();
    }
}

void TimeDisplay::onBPMEditingFinished()
{
    if (m_bpmSpin) {
        double newBpm = m_bpmSpin->value();
        if (m_controller && !m_controller->isTempoAutomated()) {
            m_controller->setBPM(newBpm);
        }
        m_bpmSpin->hide();
        update();
    }
}

} // namespace presentation::views
