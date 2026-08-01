// src/Presentation/views/mixer/instrument_slot_container.h
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include "instrument_slot_widget.h"
#include "Middle Bridge/tracks/itrack_controller.h"

namespace presentation::views {

/**
 * @brief Labeled vertical container for the single Virtual Instrument slot.
 *
 * Automatically reveals or hides itself based on whether the track is an
 * Instrument track (hasInstrumentSlot == true), and hosts an InstrumentSlotWidget.
 */
class InstrumentSlotContainer : public QWidget {
    Q_OBJECT

public:
    explicit InstrumentSlotContainer(QWidget* parent = nullptr);
    ~InstrumentSlotContainer() override = default;

    /**
     * @brief Bind the child instrument slot widget to a specific track.
     */
    void bind(bridge::ITrackController* controller, TrackID trackId);

    /**
     * @brief Refresh the instrument slot from the latest state snapshot.
     */
    void updateFromState(const bridge::TrackUIState& state);

private:
    bridge::ITrackController* m_controller = nullptr;
    TrackID                   m_trackId{};

    QVBoxLayout*          m_layout = nullptr;
    InstrumentSlotWidget* m_instrumentWidget = nullptr;
};

} // namespace presentation::views
