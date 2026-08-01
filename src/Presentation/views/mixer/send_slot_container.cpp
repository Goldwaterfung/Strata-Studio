// src/Presentation/views/mixer/send_slot_container.cpp
#include "send_slot_container.h"
#include "../theme.h"

#include <QFrame>
#include <algorithm>

namespace presentation::views {

SendSlotContainer::SendSlotContainer(bool isPreFader, QWidget* parent)
    : QWidget(parent)
    , m_isPreFader(isPreFader)
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

    // Create Scroll Area
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    
    // Style scroll bar to match DAW theme
    m_scrollArea->setStyleSheet(QString(
        "QScrollArea { background-color: transparent; border: none; }"
        "QScrollBar:vertical {"
        "  background: transparent; width: 4px; border: none; margin: 0px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: %1; border-radius: 2px; min-height: 10px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "  background: %2;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0px;"
        "}"
    )
    .arg(theme::Color::TextMuted.name())
    .arg(theme::Color::AccentGlow.name()));
    m_scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    m_scrollArea->setMinimumHeight(64);
    m_scrollArea->setMaximumHeight(64); // Display up to ~2.5 slots, keep layout compact
    
    m_scrollContent = new QWidget(m_scrollArea);
    m_scrollContent->setStyleSheet("background-color: transparent; border: none;");
    m_scrollLayout = new QVBoxLayout(m_scrollContent);
    m_scrollLayout->setContentsMargins(0, 0, 0, 0);
    m_scrollLayout->setSpacing(2);

    // Pre-allocate exactly 4 slots (since we now support exactly 4 sends)
    m_slots.reserve(4);
    for (uint32_t i = 0; i < 4; ++i) {
        auto* slot = new SendSlotWidget(m_scrollContent);
        slot->setVisible(false); // Hidden until there is an active send
        m_slots.push_back(slot);
        m_scrollLayout->addWidget(slot);
    }
    
    m_scrollArea->setWidget(m_scrollContent);
    m_scrollArea->setWidgetResizable(true);
    m_layout->addWidget(m_scrollArea);

    // Always show at least one placeholder slot
    if (!m_slots.empty()) {
        m_slots[0]->setVisible(true);
    }
}

void SendSlotContainer::bind(bridge::ITrackController* controller, TrackID trackId)
{
    m_controller = controller;
    m_trackId    = trackId;

    for (uint32_t i = 0; i < static_cast<uint32_t>(m_slots.size()); ++i) {
        m_slots[i]->bind(controller, trackId, m_isPreFader, i);
    }
}

void SendSlotContainer::updateFromState(const bridge::TrackUIState& state)
{
    if (!m_controller) return;

    uint32_t activeCount = m_isPreFader
        ? state.activePreFaderSendCount
        : state.activePostFaderSendCount;

    const bridge::SendSlotUIState* slotStates = m_isPreFader
        ? state.preFaderSends
        : state.postFaderSends;

    uint32_t numSlots = static_cast<uint32_t>(m_slots.size());

    for (uint32_t i = 0; i < numSlots; ++i) {
        if (i < activeCount) {
            m_slots[i]->setVisible(true);
            m_slots[i]->updateFromState(slotStates[i]);
        } else if (i == activeCount) {
            // Show one empty placeholder beyond the last active slot
            // so the user can see where to add more sends
            m_slots[i]->setVisible(true);
            bridge::SendSlotUIState empty{};
            empty.sendNodeId        = NodeID::invalid();
            empty.destinationNodeId = NodeID::invalid();
            empty.leveldB           = -180.0f;
            empty.panPosition       = 0.5f;
            empty.isEnabled         = false;
            std::strncpy(empty.destinationName, "-- Empty --",
                         sizeof(empty.destinationName) - 1);
            m_slots[i]->updateFromState(empty);
        } else {
            m_slots[i]->setVisible(false);
        }
    }

    m_scrollContent->adjustSize();
}

} // namespace presentation::views
