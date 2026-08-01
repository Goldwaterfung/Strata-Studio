#pragma once
#include "base_slot_widget.h"
#include <QString>
#include "Middle Bridge/tracks/itrack_controller.h"

namespace presentation::views {

/**
 * @brief Polymorphic class representing a single insert effect or virtual instrument slot.
 */
class PluginSlotWidget : public BaseSlotWidget {
    Q_OBJECT

public:
    explicit PluginSlotWidget(QWidget* parent = nullptr);
    virtual ~PluginSlotWidget() override = default;

    /**
     * @brief Refresh the widget from a fresh PluginSlotUIState snapshot.
     */
    void updateFromState(const bridge::PluginSlotUIState& slotState);

    /**
     * @brief Configure this widget as a "+" button to add a plugin.
     */
    void setAsPlusButton() override;

    bool isEmpty() const override { return m_isEmpty; }
    bool isBypassed() const override { return m_bypassed; }

    // BaseSlotWidget overrides
    QString displayLabel() const override;
    QColor getLedColor() const override;

Q_SIGNALS:
    void pluginChanged();

protected:
    // Pure virtual operations implemented by specialized slot subclasses
    virtual void toggleBypass() override = 0;
    virtual void openEditor() override = 0;
    virtual void showPluginMenu(const QPoint& globalPos) = 0;

    void showRoutingMenu(const QPoint& pos) override { showPluginMenu(pos); }

    // Display state
    QString                   m_pluginName{ "-- Empty --" };
};

} // namespace presentation::views
