// src/Presentation/views/mixer/effect_slot_container.cpp
#include "effect_slot_container.h"
#include "../theme.h"

namespace presentation::views {

EffectSlotContainer::EffectSlotContainer(QWidget* parent)
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
    m_scrollArea->setMinimumHeight(70);
    m_scrollArea->setMaximumHeight(70); // Display up to ~4 slots beautifully without scroll
    
    m_scrollContent = new QWidget(m_scrollArea);
    m_scrollContent->setStyleSheet("background-color: transparent; border: none;");
    m_scrollLayout = new QVBoxLayout(m_scrollContent);
    m_scrollLayout->setContentsMargins(1, 1, 1, 1);
    m_scrollLayout->setSpacing(2);

    // Create MAX_PLUGIN_SLOTS Slots initially hidden
    for (uint32_t i = 0; i < bridge::MAX_PLUGIN_SLOTS; ++i) {
        auto* slot = new EffectSlotWidget(m_scrollContent);
        m_scrollLayout->addWidget(slot);
        slot->setVisible(false);
        m_slots.push_back(slot);

        connect(slot, &EffectSlotWidget::pluginChanged, this, [this]() {
            if (m_controller) {
                updateFromState(m_controller->getTrackState(m_trackId));
            }
        });
    }
    
    m_scrollArea->setWidget(m_scrollContent);
    m_scrollArea->setWidgetResizable(true);
    m_layout->addWidget(m_scrollArea);
}

void EffectSlotContainer::bind(bridge::ITrackController* controller, TrackID trackId) {
    m_controller = controller;
    m_trackId = trackId;

    for (uint32_t i = 0; i < m_slots.size(); ++i) {
        m_slots[i]->bind(controller, trackId, i);
    }
}

void EffectSlotContainer::updateFromState(const bridge::TrackUIState& state) {
    if (!m_controller) return;

    // Count active plugins
    uint32_t activeCount = 0;
    std::vector<uint32_t> activeIndices;
    for (uint32_t i = 0; i < bridge::MAX_PLUGIN_SLOTS; ++i) {
        if (state.plugins[i].pluginNodeId.isValid()) {
            activeCount++;
            activeIndices.push_back(i);
        }
    }

    uint32_t totalVisible = activeCount;
    if (activeCount < bridge::MAX_PLUGIN_SLOTS) {
        totalVisible += 1; // Show "+" slot
    }

    // Configure and show active slots
    for (uint32_t i = 0; i < activeCount; ++i) {
        uint32_t originalSlotIndex = activeIndices[i];
        m_slots[i]->bind(m_controller, m_trackId, originalSlotIndex);
        m_slots[i]->updateFromState(state.plugins[originalSlotIndex]);
        m_slots[i]->setVisible(true);
    }

    // Configure and show the "+" button slot if needed
    if (activeCount < bridge::MAX_PLUGIN_SLOTS) {
        // Find first empty index in state
        uint32_t firstEmptyIndex = 0;
        for (uint32_t j = 0; j < bridge::MAX_PLUGIN_SLOTS; ++j) {
            if (!state.plugins[j].pluginNodeId.isValid()) {
                firstEmptyIndex = j;
                break;
            }
        }
        m_slots[activeCount]->bind(m_controller, m_trackId, firstEmptyIndex);
        m_slots[activeCount]->setAsPlusButton();
        m_slots[activeCount]->setVisible(true);
    }

    // Hide any unused slot widgets
    for (uint32_t i = totalVisible; i < m_slots.size(); ++i) {
        m_slots[i]->setVisible(false);
    }

    m_scrollContent->adjustSize();
}

} // namespace presentation::views
