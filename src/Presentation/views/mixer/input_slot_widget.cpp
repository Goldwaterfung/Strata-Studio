// src/Presentation/views/mixer/input_slot_widget.cpp
#include "input_slot_widget.h"
#include "../theme.h"

#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QMouseEvent>
#include <QMenu>
#include <QAction>

namespace presentation::views {

InputSlotWidget::InputSlotWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(22);
    setMinimumWidth(80);
    setMaximumWidth(240);
    setCursor(Qt::PointingHandCursor);
    setToolTip("Track Input — click to select input device / channel");
}

void InputSlotWidget::bind(bridge::ITrackController* controller, TrackID trackId) {
    m_controller = controller;
    m_trackId = trackId;
}

void InputSlotWidget::updateFromState(const bridge::TrackInputUIState& state) {
    m_inputName = QString::fromUtf8(state.inputName);
    update();
}

void InputSlotWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    double w = static_cast<double>(width());
    double h = static_cast<double>(height());

    QColor bg = theme::Color::BgSlotEmpty;
    painter.fillRect(rect(), bg);

    QColor border = theme::Color::BorderSlotEmpty;
    painter.setPen(QPen(border, 1.0));
    painter.drawRoundedRect(QRectF(0.5, 0.5, w - 1.0, h - 1.0), 2.0, 2.0);

    QColor textColor = theme::Color::TextPrimary;
    painter.setPen(textColor);
    painter.setFont(theme::Font::monospace(8, QFont::Normal));

    QRectF labelRect(4.0, 1.0, w - 8.0, h - 2.0);
    painter.drawText(labelRect, Qt::AlignVCenter | Qt::AlignCenter, m_inputName);
}

void InputSlotWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        showInputMenu(event->globalPosition().toPoint());
    }
}

void InputSlotWidget::showInputMenu(const QPoint& globalPos) {
    if (!m_controller) return;

    QMenu menu;
    menu.setStyleSheet(theme::Style::getGlobalStyleSheet());
    auto options = m_controller->getAvailableTrackInputs(m_trackId);

    QAction* noneAction = menu.addAction("None");
    QObject::connect(noneAction, &QAction::triggered, [this]() {
        m_controller->setTrackInput(m_trackId, 0xFFFFFFFF, 0);
    });

    menu.addSeparator();

    for (const auto& opt : options) {
        QAction* action = menu.addAction(QString::fromStdString(opt.name));
        QObject::connect(action, &QAction::triggered, [this, opt]() {
            m_controller->setTrackInput(m_trackId, opt.optionId, opt.numChannels);
        });
    }

    menu.exec(globalPos);
}

} // namespace presentation::views
