// src/Presentation/views/mixer/instrument_slot_widget.cpp
#include "instrument_slot_widget.h"
#include "plugin_editor_dialog.h"
#include "../theme.h"
#include <QMenu>
#include <QAction>

namespace presentation::views {

InstrumentSlotWidget::InstrumentSlotWidget(QWidget* parent)
    : PluginSlotWidget(parent)
{
    setToolTip("Virtual Instrument Slot — click to load / edit synths or samplers");
}

void InstrumentSlotWidget::toggleBypass() {
    if (!m_controller) return;
    m_bypassed = !m_bypassed;
    m_controller->setInstrumentBypassed(m_trackId, m_bypassed);
}

void InstrumentSlotWidget::openEditor() {
    if (!m_controller) return;

    uint8_t category = PluginCategory::INSTRUMENT;

    // Instantiated with isInstrument = true
    auto* dialog = new PluginEditorDialog(m_controller, m_trackId, 0, m_pluginName, category, window(), true);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void InstrumentSlotWidget::showPluginMenu(const QPoint& globalPos) {
    QMenu menu(this);
    menu.setStyleSheet(theme::Style::getGlobalStyleSheet());
    menu.setProperty("class", "instrumentMenu");

    QAction* removeAct = nullptr;
    if (!m_isEmpty) {
        removeAct = menu.addAction("Remove Instrument");
        menu.addSeparator();
    }

    if (m_controller) {
        std::vector<PluginDescriptor> plugins = m_controller->getAvailablePlugins();

        bool hasInstruments = false;
        for (const auto& plug : plugins) {
            if (plug.category == PluginCategory::INSTRUMENT) {
                hasInstruments = true;
                QAction* act = menu.addAction(QString::fromUtf8(plug.name));
                act->setData(QVariant(plug.pluginId));
            }
        }

        if (!hasInstruments) {
            QAction* warn = menu.addAction("(No scanned instruments found)");
            warn->setEnabled(false);
        }
    }

    QAction* chosen = menu.exec(globalPos);
    if (!chosen || !m_controller) return;

    if (chosen == removeAct) {
        m_controller->removeInstrument(m_trackId);
        Q_EMIT pluginChanged();
    } else {
        uint32_t pluginId = chosen->data().toUInt();
        if (pluginId > 0) {
            m_controller->insertInstrument(m_trackId, pluginId);
            Q_EMIT pluginChanged();
            openEditor();
        }
    }
    update();
}

void InstrumentSlotWidget::setAsPlusButton() {
    m_isPlusButton = true;
    m_isEmpty = true;
    m_pluginName = "+ add instrument";
    update();
}

QString InstrumentSlotWidget::displayLabel() const {
    if (m_isPlusButton) return m_pluginName;
    return m_isEmpty ? QString("+ add instrument") : m_pluginName;
}

} // namespace presentation::views
