// src/Presentation/views/mixer/effect_slot_container.h
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <vector>
#include "effect_slot_widget.h"
#include "Middle Bridge/tracks/itrack_controller.h"

namespace presentation::views {

/**
 * @brief A labeled vertical container for EffectSlotWidgets.
 *
 * Displays a section header ("INSERT EFFECTS") above a vertical stack
 * of up to MAX_PLUGIN_SLOTS EffectSlotWidget instances.
 */
class EffectSlotContainer : public QWidget {
    Q_OBJECT

public:
    explicit EffectSlotContainer(QWidget* parent = nullptr);
    ~EffectSlotContainer() override = default;

    /**
     * @brief Bind all child plugin slot widgets to a specific track.
     */
    void bind(bridge::ITrackController* controller, TrackID trackId);

    /**
     * @brief Refresh all child plugin slot widgets from the latest state snapshot.
     */
    void updateFromState(const bridge::TrackUIState& state);

private:
    bridge::ITrackController* m_controller = nullptr;
    TrackID                   m_trackId{};

    QVBoxLayout*                m_layout = nullptr;
    QScrollArea*                m_scrollArea = nullptr;
    QWidget*                    m_scrollContent = nullptr;
    QVBoxLayout*                m_scrollLayout = nullptr;
    std::vector<EffectSlotWidget*> m_slots;
};

} // namespace presentation::views
