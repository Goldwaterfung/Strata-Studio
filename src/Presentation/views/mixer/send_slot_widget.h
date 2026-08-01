// src/Presentation/views/mixer/send_slot_widget.h
#pragma once

#include "base_slot_widget.h"
#include <QString>
#include "Middle Bridge/tracks/itrack_controller.h"

namespace presentation::views {

/**
 * @brief A compact, draggable auxiliary send level widget.
 */
class SendSlotWidget : public BaseSlotWidget {
    Q_OBJECT

public:
    explicit SendSlotWidget(QWidget* parent = nullptr);
    ~SendSlotWidget() override = default;

    /**
     * @brief Bind this widget to a specific send slot.
     */
    void bind(bridge::ITrackController* controller,
              TrackID trackId, bool isPreFader, uint32_t slotIndex);

    /**
     * @brief Refresh the widget from a fresh TrackUIState snapshot.
     */
    void updateFromState(const bridge::SendSlotUIState& slotState);

    bool isEmpty() const override { return m_isEmpty; }
    bool isBypassed() const override { return m_bypassed; }

    // BaseSlotWidget overrides
    QString displayLabel() const override;
    QColor getLedColor() const override;
    void setAsPlusButton() override;

Q_SIGNALS:
    void sendLevelChanged(float gainLinear);
    void sendEnabledToggled(bool enabled);

protected:
    // BaseSlotWidget overrides
    void toggleBypass() override;
    void openEditor() override;
    void showRoutingMenu(const QPoint& pos) override;
    void paintAdditional(QPainter* painter, double w, double h) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    bool      m_isPreFader = false;
    uint32_t  m_slotIndex  = 0;

    // Display state (updated from bridge at 60 Hz)
    float   m_leveldB        = -180.0f;
    float   m_panPosition    = 0.5f;
    QString m_destinationName{ "-- Empty --" };

    static constexpr float k_minDb = -60.0f;
    static constexpr float k_maxDb =  12.0f;
};

} // namespace presentation::views
