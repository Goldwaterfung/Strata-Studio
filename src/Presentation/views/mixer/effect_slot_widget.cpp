// src/Presentation/views/mixer/effect_slot_widget.cpp
#include "effect_slot_widget.h"
#include "plugin_editor_dialog.h"
#include "../theme.h"
#include <QMenu>
#include <QAction>

namespace presentation::views {

EffectSlotWidget::EffectSlotWidget(QWidget* parent)
    : PluginSlotWidget(parent)
{
    setToolTip("Insert Effect Slot — click to load / edit effects");
}

void EffectSlotWidget::bind(bridge::ITrackController* controller, TrackID trackId, uint32_t slotIndex) {
    PluginSlotWidget::bind(controller, trackId);
    m_slotIndex = slotIndex;
}

void EffectSlotWidget::toggleBypass() {
    if (!m_controller) return;
    m_bypassed = !m_bypassed;

    std::vector<bridge::TrackUIState> tracks = m_controller->getAllTracks();
    std::vector<TrackID> selectedTracks;
    bool isSelected = false;
    for (const auto& t : tracks) {
        if (t.isSelected) {
            selectedTracks.push_back(t.trackId);
            if (t.trackId == m_trackId) isSelected = true;
        }
    }
    if (!isSelected) {
        selectedTracks = {m_trackId};
    }

    for (TrackID id : selectedTracks) {
        m_controller->setPluginBypassed(id, m_slotIndex, m_bypassed);
    }
}

void EffectSlotWidget::openEditor() {
    if (!m_controller) return;

    // Look up the category from the list of scanned plugins
    uint8_t category = 0; // EFFECT_OTHER
    auto list = m_controller->getAvailablePlugins();
    for (const auto& plug : list) {
        if (m_pluginName == QString::fromUtf8(plug.name)) {
            category = plug.category;
            break;
        }
    }

    // Instantiated with isInstrument = false
    auto* dialog = new PluginEditorDialog(m_controller, m_trackId, m_slotIndex, m_pluginName, category, window(), false);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void EffectSlotWidget::showPluginMenu(const QPoint& globalPos) {
    QMenu menu(this);
    menu.setStyleSheet(theme::Style::getGlobalStyleSheet());
    menu.setProperty("class", "effectMenu");

    QAction* removeAct = nullptr;
    QAction* duplicateAct = nullptr;
    if (!m_isEmpty) {
        duplicateAct = menu.addAction("Duplicate to Selected Tracks");
        removeAct = menu.addAction("Remove Effect");
        menu.addSeparator();
    }

    if (m_controller) {
        std::vector<PluginDescriptor> plugins = m_controller->getAvailablePlugins();

        if (plugins.empty()) {
            QAction* warn = menu.addAction("(No scanned plugins found)");
            warn->setEnabled(false);
        } else {
            // Group effects by category
            QMenu* delayReverbMenu = nullptr;
            QMenu* distortionMenu = nullptr;
            QMenu* dynamicsMenu = nullptr;
            QMenu* eqFilterMenu = nullptr;
            QMenu* modulationMenu = nullptr;
            QMenu* othersMenu = nullptr;

            for (const auto& plug : plugins) {
                QMenu* targetMenu = nullptr;
                if (plug.category == PluginCategory::INSTRUMENT) {
                    // Skip instruments in insert effect slots
                    continue;
                } else if (plug.category == PluginCategory::EFFECT_DELAY_REVERB) {
                    if (!delayReverbMenu) delayReverbMenu = menu.addMenu("Delay / Reverb");
                    targetMenu = delayReverbMenu;
                } else if (plug.category == PluginCategory::EFFECT_DISTORTION) {
                    if (!distortionMenu) distortionMenu = menu.addMenu("Distortion");
                    targetMenu = distortionMenu;
                } else if (plug.category == PluginCategory::EFFECT_DYNAMICS) {
                    if (!dynamicsMenu) dynamicsMenu = menu.addMenu("Dynamics");
                    targetMenu = dynamicsMenu;
                } else if (plug.category == PluginCategory::EFFECT_EQ_FILTER) {
                    if (!eqFilterMenu) eqFilterMenu = menu.addMenu("EQ / Filter");
                    targetMenu = eqFilterMenu;
                } else if (plug.category == PluginCategory::EFFECT_MODULATION) {
                    if (!modulationMenu) modulationMenu = menu.addMenu("Modulation");
                    targetMenu = modulationMenu;
                } else {
                    if (!othersMenu) othersMenu = menu.addMenu("Others");
                    targetMenu = othersMenu;
                }

                if (targetMenu) {
                    QAction* act = targetMenu->addAction(QString::fromUtf8(plug.name));
                    act->setData(QVariant(plug.pluginId));
                }
            }
        }
    }

    QAction* chosen = menu.exec(globalPos);
    if (!chosen || !m_controller) return;

    std::vector<bridge::TrackUIState> tracks = m_controller->getAllTracks();
    std::vector<TrackID> selectedTracks;
    bool isSelected = false;
    for (const auto& t : tracks) {
        if (t.isSelected) {
            selectedTracks.push_back(t.trackId);
            if (t.trackId == m_trackId) isSelected = true;
        }
    }
    if (!isSelected) {
        selectedTracks = {m_trackId};
    }

    if (chosen == removeAct) {
        for (TrackID id : selectedTracks) {
            m_controller->removePlugin(id, m_slotIndex);
        }
        Q_EMIT pluginChanged();
    } else if (chosen == duplicateAct) {
        std::vector<uint8_t> state = m_controller->getPluginState(m_trackId, m_slotIndex);
        
        uint32_t currentPluginId = 0;
        auto list = m_controller->getAvailablePlugins();
        for (const auto& plug : list) {
            if (m_pluginName == QString::fromUtf8(plug.name)) {
                currentPluginId = plug.pluginId;
                break;
            }
        }
        
        if (currentPluginId > 0) {
            for (TrackID id : selectedTracks) {
                if (id == m_trackId) continue;
                m_controller->insertPlugin(id, m_slotIndex, currentPluginId);
                m_controller->setPluginState(id, m_slotIndex, state);
            }
            Q_EMIT pluginChanged();
        }
    } else {
        uint32_t pluginId = chosen->data().toUInt();
        if (pluginId > 0) {
            for (TrackID id : selectedTracks) {
                m_controller->insertPlugin(id, m_slotIndex, pluginId);
            }
            Q_EMIT pluginChanged();
        }
    }
    update();
}

} // namespace presentation::views
