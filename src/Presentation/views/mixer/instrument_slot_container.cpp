// src/Presentation/views/mixer/instrument_slot_container.cpp
#include "instrument_slot_container.h"
#include "../theme.h"
#include <QFrame>

namespace presentation::views {

InstrumentSlotContainer::InstrumentSlotContainer(QWidget* parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(2);

    // Horizontal group divider
    auto* divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Plain);
    divider->setStyleSheet(QString("color: %1; margin-bottom: 2px;").arg(theme::Color::BorderSlotEmpty.name()));
    m_layout->addWidget(divider);

    // Single Instrument Slot
    m_instrumentWidget = new InstrumentSlotWidget(this);
    m_layout->addWidget(m_instrumentWidget);

    connect(m_instrumentWidget, &InstrumentSlotWidget::pluginChanged, this, [this]() {
        if (m_controller) {
            updateFromState(m_controller->getTrackState(m_trackId));
        }
    });
}

void InstrumentSlotContainer::bind(bridge::ITrackController* controller, TrackID trackId) {
    m_controller = controller;
    m_trackId = trackId;
    m_instrumentWidget->bind(controller, trackId);
}

void InstrumentSlotContainer::updateFromState(const bridge::TrackUIState& state) {
    if (!m_controller) return;

    if (!state.hasInstrumentSlot) {
        setVisible(false);
        return;
    }

    setVisible(true);
    m_instrumentWidget->bind(m_controller, m_trackId);

    if (state.instrument.pluginNodeId.isValid()) {
        m_instrumentWidget->updateFromState(state.instrument);
    } else {
        m_instrumentWidget->setAsPlusButton();
    }
}

} // namespace presentation::views
