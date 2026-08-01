// src/Presentation/views/mixer/base_slot_widget.cpp
#include "base_slot_widget.h"
#include "../theme.h"

namespace presentation::views {

BaseSlotWidget::BaseSlotWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(28);
    setMinimumWidth(80);
    setMaximumWidth(240);
    setCursor(Qt::PointingHandCursor);
}

void BaseSlotWidget::bind(bridge::ITrackController* controller, TrackID trackId) {
    m_controller = controller;
    m_trackId = trackId;
}

QRectF BaseSlotWidget::bypassButtonRect() const {
    double h = static_cast<double>(height());
    double r = 10.0;
    return QRectF(6.0, (h - r) / 2.0, r, r);
}

void BaseSlotWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    double w = static_cast<double>(width());
    double h = static_cast<double>(height());

    if (m_isPlusButton) {
        QColor bg = theme::Color::BgSlotEmpty;
        painter.fillRect(rect(), bg);

        QColor borderColor = theme::Color::BorderSlotBypassed;
        QPen pen(borderColor, 1.0, Qt::DashLine);
        painter.setPen(pen);
        painter.drawRoundedRect(QRectF(0.5, 0.5, w - 1.0, h - 1.0), 3.0, 3.0);

        painter.setPen(theme::Color::TextSecondary);
        painter.setFont(theme::Font::monospace(9, QFont::Bold));
        painter.drawText(rect(), Qt::AlignCenter, displayLabel());
        return;
    }

    QColor bg = m_isEmpty
        ? theme::Color::BgSlotEmpty
        : (m_bypassed ? theme::Color::BgSlotBypassed : theme::Color::BgSlotActive);
    painter.fillRect(rect(), bg);

    QColor border = m_isEmpty
        ? theme::Color::BorderSlotEmpty
        : (m_bypassed ? theme::Color::BorderSlotBypassed : theme::Color::BorderSlotActive);
    painter.setPen(QPen(border, 1.0));
    painter.drawRoundedRect(QRectF(0.5, 0.5, w - 1.0, h - 1.0), 2.0, 2.0);

    QRectF bypassRect = bypassButtonRect();
    QColor circleColor = getLedColor();
    painter.setBrush(circleColor);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(bypassRect);

    QString label = displayLabel();
    QColor textColor = m_isEmpty
        ? theme::Color::TextMuted
        : (m_bypassed ? theme::Color::TextMuted : theme::Color::TextPrimary);

    painter.setPen(textColor);
    painter.setFont(theme::Font::monospace(7, QFont::Bold));

    double labelX = bypassRect.right() + 8.0;
    QRectF labelRect(labelX, 1.0, w - labelX - 4.0, h - 2.0);
    painter.drawText(labelRect, Qt::AlignVCenter | Qt::AlignLeft, label);

    paintAdditional(&painter, w, h);
}

void BaseSlotWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (m_isPlusButton) {
            showRoutingMenu(event->globalPosition().toPoint());
            return;
        }

        if (bypassButtonRect().contains(event->position())) {
            if (!m_isEmpty) {
                toggleBypass();
                update();
            }
            return;
        }
        
        if (m_isEmpty) {
            showRoutingMenu(event->globalPosition().toPoint());
        } else {
            openEditor();
        }
    }
}

void BaseSlotWidget::contextMenuEvent(QContextMenuEvent* event) {
    if (!m_isPlusButton) {
        showRoutingMenu(event->globalPos());
    }
}

void BaseSlotWidget::paintAdditional(QPainter* /*painter*/, double /*w*/, double /*h*/) {
    // Optional hook for subclasses
}

} // namespace presentation::views
