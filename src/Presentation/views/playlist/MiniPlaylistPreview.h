// src/Presentation/views/playlist/MiniPlaylistPreview.h
#pragma once

#include <QWidget>
#include "timeline/iarrangement_controller.h"
#include "timeline/itimeline_controller.h"

namespace presentation::views {

/**
 * @brief Compact read-only birds-eye representation overlay positioned below the track canvas scrollbar.
 *
 * Adheres strictly to Phase 10. Highlights:
 *  - Single-pass wide viewport culling queries using stack-allocated structures.
 *  - Highlighted semi-transparent range overlay bracket tracking the main view.
 *  - Click and drag seek integration to scrub active viewport focus coordinates.
 */
class MiniPlaylistPreview : public QWidget {
    Q_OBJECT
public:
    explicit MiniPlaylistPreview(
        bridge::IArrangementController* arrangement,
        bridge::ITimelineController*    timeline,
        QWidget* parent = nullptr);
    ~MiniPlaylistPreview() override = default;

    /**
     * @brief Update visible view boundaries to reposition the overlay bracket.
     */
    void setViewport(uint64_t startFrame, uint64_t endFrame);

signals:
    /**
     * @brief Fired when the user clicks or scrubs on the preview.
     */
    void viewportSeekRequested(uint64_t newStartFrame);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void handleSeek(double mouseX);

private:
    static constexpr uint32_t PREVIEW_MAX_REGIONS = 512;
    bridge::IArrangementController* m_arrangement{nullptr};
    bridge::ITimelineController*    m_timeline{nullptr};
    uint64_t m_totalFrames{14400000}; // Default: 5 mins at 48kHz
    uint64_t m_viewStart{0};
    uint64_t m_viewEnd{480000};
};

} // namespace presentation::views
