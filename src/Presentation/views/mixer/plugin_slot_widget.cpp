// src/Presentation/views/mixer/plugin_slot_widget.cpp
#include "plugin_slot_widget.h"
#include "../theme.h"

namespace presentation::views {

PluginSlotWidget::PluginSlotWidget(QWidget* parent)
    : BaseSlotWidget(parent)
{
    setToolTip("Plugin Slot — click to open menu / toggle bypass");
}

void PluginSlotWidget::setAsPlusButton() {
    m_isPlusButton = true;
    m_isEmpty = true;
    m_pluginName = "+ add effect";
    update();
}

void PluginSlotWidget::updateFromState(const bridge::PluginSlotUIState& slotState) {
    m_isPlusButton = false;
    m_bypassed = slotState.bypassed;
    m_isEmpty = !slotState.pluginNodeId.isValid();
    m_pluginName = QString::fromUtf8(slotState.pluginName);
    update();
}

QString PluginSlotWidget::displayLabel() const {
    if (m_isPlusButton) return m_pluginName;
    return m_isEmpty ? QString("+ add effect") : m_pluginName;
}

QColor PluginSlotWidget::getLedColor() const {
    if (m_isEmpty) {
        return theme::Color::BgControl;
    }
    return m_bypassed ? theme::Color::PluginBypassed : theme::Color::PluginActive;
}

} // namespace presentation::views
