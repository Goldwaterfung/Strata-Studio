// src/Presentation/views/mixer/send_slot_widget.cpp
#include "send_slot_widget.h"
#include "send_editor_popup.h"
#include "../theme.h"
#include "common/math/gain.h"

#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <cmath>
#include <algorithm>

namespace presentation::views {

SendSlotWidget::SendSlotWidget(QWidget* parent)
    : BaseSlotWidget(parent)
{
    setToolTip("Send Slot — click to open routing/parameters · right-click for options");
}

void SendSlotWidget::bind(bridge::ITrackController* controller,
                          TrackID trackId, bool isPreFader, uint32_t slotIndex)
{
    BaseSlotWidget::bind(controller, trackId);
    m_isPreFader  = isPreFader;
    m_slotIndex   = slotIndex;
}

void SendSlotWidget::setAsPlusButton() {
    m_isPlusButton = true;
    m_isEmpty = true;
    update();
}

void SendSlotWidget::updateFromState(const bridge::SendSlotUIState& slotState)
{
    m_isPlusButton    = false;
    m_leveldB         = slotState.leveldB;
    m_panPosition     = slotState.panPosition;
    m_bypassed        = !slotState.isEnabled;
    m_isEmpty         = !slotState.destinationNodeId.isValid();
    m_destinationName = QString::fromUtf8(slotState.destinationName);
    update();
}

QString SendSlotWidget::displayLabel() const {
    if (m_isPlusButton) {
        return m_isPreFader ? QString("+ pre-send") : QString("+ post-send");
    }
    return m_isEmpty
        ? (m_isPreFader ? QString("+ pre-send") : QString("+ post-send"))
        : m_destinationName;
}

QColor SendSlotWidget::getLedColor() const {
    if (m_isEmpty) {
        return theme::Color::BgControl;
    }
    return m_bypassed
        ? theme::Color::PluginBypassed
        : (m_isPreFader ? theme::Color::SendPreFader : theme::Color::SendPostFader);
}

void SendSlotWidget::toggleBypass() {
    if (!m_controller) return;
    m_bypassed = !m_bypassed;
    bool enabled = !m_bypassed;
    Q_EMIT sendEnabledToggled(enabled);

    std::vector<bridge::TrackUIState> tracks = m_controller->getAllTracks();
    bool isSelected = false;
    std::vector<TrackID> selectedTracks;
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
        m_controller->setSendEnabled(id, m_isPreFader, m_slotIndex, enabled);
    }
}

void SendSlotWidget::openEditor()
{
    auto* popup = new SendEditorPopup(m_controller, m_trackId, m_isPreFader, m_slotIndex, this);
    
    // Position the popup centered directly above the slot widget
    QPoint globalPos = mapToGlobal(QPoint(0, 0));
    int x = globalPos.x() + (width() - popup->width()) / 2;
    int y = globalPos.y() - popup->height() - 4;
    popup->move(x, y);
    popup->show();
}

void SendSlotWidget::showRoutingMenu(const QPoint& pos)
{
    if (!m_controller) return;

    QMenu menu(this);
    menu.setStyleSheet(theme::Style::getGlobalStyleSheet());

    QAction* noRoute = menu.addAction("-- None (disconnect) --");
    menu.addSeparator();

    std::vector<bridge::TrackUIState> tracks = m_controller->getAllTracks();

    for (const auto& t : tracks) {
        if (t.trackId != m_trackId && (t.type == composition::TrackType::AUX || t.type == composition::TrackType::MASTER)) {
            QAction* act = menu.addAction(QString::fromUtf8(t.name));
            QVariantList data;
            data << QVariant(static_cast<qlonglong>(t.trackId.toRaw())) << QVariant(QString::fromUtf8(t.name));
            act->setData(data);
            if (!m_isEmpty && t.name == m_destinationName) {
                act->setCheckable(true);
                act->setChecked(true);
            }
        }
    }

    menu.addSeparator();
    QAction* createAuxAct = menu.addAction("Create & Send to New Aux Track");

    QAction* chosen = menu.exec(pos);

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

    if (chosen == noRoute) {
        for (TrackID id : selectedTracks) {
            m_controller->setSendDestination(
                id, m_isPreFader, m_slotIndex, NodeID::invalid());
        }
        m_isEmpty         = true;
        m_destinationName = "-- Empty --";
    } else if (chosen == createAuxAct) {
        // Create new Aux track exactly once
        int nextIndex = static_cast<int>(tracks.size());
        QString auxName = QString("Aux %1").arg(nextIndex);
        TrackID newAuxTrackId = m_controller->addAuxTrack(auxName.toUtf8().constData(), 0xFF3B82F6); // Blue color theme
        
        if (newAuxTrackId.isValid()) {
            NodeID destNodeId = NodeID::fromRaw(newAuxTrackId.toRaw());
            for (TrackID id : selectedTracks) {
                m_controller->setSendDestination(id, m_isPreFader, m_slotIndex, destNodeId);
            }
            m_isEmpty         = false;
            m_destinationName = auxName;
        }
    } else if (chosen) {
        QVariantList data = chosen->data().toList();
        if (data.size() >= 2) {
            uint64_t rawId = static_cast<uint64_t>(data[0].toLongLong());
            NodeID destNodeId = NodeID::fromRaw(rawId);
            for (TrackID id : selectedTracks) {
                m_controller->setSendDestination(
                    id, m_isPreFader, m_slotIndex, destNodeId);
            }
            m_isEmpty         = false;
            m_destinationName = data[1].toString();
        }
    }
    update();
}

void SendSlotWidget::paintAdditional(QPainter* painter, double w, double h) {
    if (!m_isEmpty && !m_bypassed) {
        QColor indicatorColor = m_isPreFader ? theme::Color::SendPreFader : theme::Color::SendPostFader;
        double val = static_cast<double>(m_leveldB);
        double minDbVal = static_cast<double>(k_minDb);
        double maxDbVal = static_cast<double>(k_maxDb);
        double normalized = (std::clamp(val, minDbVal, maxDbVal) - minDbVal) / (maxDbVal - minDbVal);
        double fillW = (w - 2.0) * normalized;
        painter->fillRect(QRectF(1.0, h - 3.0, fillW, 2.0), indicatorColor);
    }
}

void SendSlotWidget::contextMenuEvent(QContextMenuEvent* event)
{
    if (!m_controller) return;

    if (!m_isEmpty) {
        QMenu menu(this);
        menu.setStyleSheet(theme::Style::getGlobalStyleSheet());

        QAction* bypassAct = menu.addAction(m_bypassed ? "Enable Send" : "Bypass Send");
        QAction* removeAct = menu.addAction("Remove Send");

        QAction* chosen = menu.exec(event->globalPos());

        if (chosen == bypassAct) {
            m_bypassed = !m_bypassed;
            bool enabled = !m_bypassed;
            Q_EMIT sendEnabledToggled(enabled);
            std::vector<bridge::TrackUIState> tracks = m_controller->getAllTracks();
            bool isSelected = false;
            std::vector<TrackID> selectedTracks;
            for (const auto& t : tracks) {
                if (t.isSelected) {
                    selectedTracks.push_back(t.trackId);
                    if (t.trackId == m_trackId) isSelected = true;
                }
            }
            if (!isSelected) selectedTracks = {m_trackId};
            
            for (TrackID id : selectedTracks) {
                m_controller->setSendEnabled(id, m_isPreFader, m_slotIndex, enabled);
            }
            update();
        } else if (chosen == removeAct) {
            std::vector<bridge::TrackUIState> tracks = m_controller->getAllTracks();
            bool isSelected = false;
            std::vector<TrackID> selectedTracks;
            for (const auto& t : tracks) {
                if (t.isSelected) {
                    selectedTracks.push_back(t.trackId);
                    if (t.trackId == m_trackId) isSelected = true;
                }
            }
            if (!isSelected) selectedTracks = {m_trackId};

            for (TrackID id : selectedTracks) {
                m_controller->setSendDestination(id, m_isPreFader, m_slotIndex, NodeID::invalid());
            }
            m_isEmpty         = true;
            m_destinationName = "-- Empty --";
            update();
        }
    } else {
        showRoutingMenu(event->globalPos());
    }
}

} // namespace presentation::views
