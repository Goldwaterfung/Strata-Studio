// src/Presentation/views/rotary_dial.h
#pragma once

#include "base_tactile_control.h"
#include <QPixmap>

namespace presentation::views {

/**
 * @brief A premium, hardware-inspired custom circular knob widget with volumetric glows.
 * Utilizes background-foreground split caching for zero-overhead vector paint cycles.
 */
class RotaryDial : public BaseTactileControl {
    Q_OBJECT

public:
    explicit RotaryDial(QWidget* parent = nullptr);
    ~RotaryDial() override = default;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    /**
     * @brief Generates the static tick marks and backing dial graphics into the cached pixmap.
     */
    void renderStaticBackground();

    QPixmap m_bgCache;
    bool m_bgCacheValid = false;
};

} // namespace presentation::views
