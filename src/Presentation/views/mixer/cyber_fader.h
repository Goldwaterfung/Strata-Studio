// src/Presentation/views/cyber_fader.h
#pragma once

#include "base_tactile_control.h"
#include <QPixmap>

namespace presentation::views {

/**
 * @brief A premium, custom vertical volume fader widget.
 * Features a recessed slot, linear graduation marks, and a dynamic glowing grip handle.
 */
class CyberFader : public BaseTactileControl {
    Q_OBJECT

public:
    explicit CyberFader(QWidget* parent = nullptr);
    ~CyberFader() override = default;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    /**
     * @brief Renders the fader's vertical channel track groove and logarithmic dB scale tick lines.
     */
    void renderStaticBackground();

    QPixmap m_bgCache;
    bool m_bgCacheValid = false;
};

} // namespace presentation::views
