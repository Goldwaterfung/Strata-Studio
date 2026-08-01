// src/Presentation/views/mixer/send_slot_container.h
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <vector>
#include "send_slot_widget.h"
#include "Middle Bridge/tracks/itrack_controller.h"

namespace presentation::views {

/**
 * @brief A labelled vertical container for SendSlotWidgets.
 *
 * Displays a section header ("SENDS – PRE" or "SENDS – POST") above a
 * vertical stack of up to MAX_SEND_SLOTS SendSlotWidget instances.
 * Empty slots render a faint dashed placeholder until they are routed.
 *
 * Layer 7 constraint: all data comes from bridge::TrackUIState.
 */
class SendSlotContainer : public QWidget {
    Q_OBJECT

public:
    explicit SendSlotContainer(bool isPreFader, QWidget* parent = nullptr);
    ~SendSlotContainer() override = default;

    /**
     * @brief Bind all child send slot widgets to a specific track.
     */
    void bind(bridge::ITrackController* controller, TrackID trackId);

    /**
     * @brief Refresh all child send slot widgets from the latest state snapshot.
     *        Called by the PresentationDirector at 60 Hz.
     */
    void updateFromState(const bridge::TrackUIState& state);

private:
    bool m_isPreFader;
    bridge::ITrackController* m_controller = nullptr;
    TrackID m_trackId{};

    QVBoxLayout* m_layout  = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget*     m_scrollContent = nullptr;
    QVBoxLayout* m_scrollLayout = nullptr;
    std::vector<SendSlotWidget*> m_slots;
};

} // namespace presentation::views
