// src/Presentation/views/mixer/effect_slot_widget.h
#pragma once

#include "plugin_slot_widget.h"

namespace presentation::views {

class EffectSlotWidget : public PluginSlotWidget {
    Q_OBJECT

public:
    explicit EffectSlotWidget(QWidget* parent = nullptr);
    ~EffectSlotWidget() override = default;

    void bind(bridge::ITrackController* controller, TrackID trackId, uint32_t slotIndex);

protected:
    void toggleBypass() override;
    void openEditor() override;
    void showPluginMenu(const QPoint& globalPos) override;

private:
    uint32_t m_slotIndex = 0;
};

} // namespace presentation::views
