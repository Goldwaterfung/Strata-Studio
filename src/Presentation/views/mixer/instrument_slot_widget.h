// src/Presentation/views/mixer/instrument_slot_widget.h
#pragma once

#include "plugin_slot_widget.h"

namespace presentation::views {

class InstrumentSlotWidget : public PluginSlotWidget {
    Q_OBJECT

public:
    explicit InstrumentSlotWidget(QWidget* parent = nullptr);
    ~InstrumentSlotWidget() override = default;

    void setAsPlusButton() override;
    QString displayLabel() const override;

protected:
    void toggleBypass() override;
    void openEditor() override;
    void showPluginMenu(const QPoint& globalPos) override;
};

} // namespace presentation::views
