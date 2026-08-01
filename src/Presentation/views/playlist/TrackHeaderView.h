// src/Presentation/views/playlist/TrackHeaderView.h
#pragma once

#include <QWidget>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <array>
#include "common/system_primitives.h"
#include "tracks/itrack_controller.h"
#include "telemetry/imetering_provider.h"
#include "TrackRowRenderer.h" // For VirtualControl

class QPushButton;
class QLineEdit;

namespace bridge {
    class IArrangementController;
    class IAutomationController;
}

namespace presentation::views {

enum class RowType {
    Track,
    Take,
    Automation
};

enum class DropAction {
    None,
    InsertBefore,
    InsertAfter,
    DropInto
};

struct RowLayout {
    RowType type;
    TrackID trackId;
    uint32_t index{0}; // takeLaneIndex or subLaneIndex
    double localY{0.0};
    double height{0.0};

    // Widget-space boundaries (offset by scroll)
    QRectF rect;

    // Track type control bounds
    QRectF muteRect;
    QRectF soloRect;
    QRectF armRect;
    QRectF monitorRect;
    QRectF autoComboRect;
    QRectF autoExpandRect;
    QRectF takesExpandRect;
    QRectF nameRect;

    // Take type control bounds
    QRectF promoteRect;
};

class TrackHeaderView : public QWidget {
    Q_OBJECT
public:
    static constexpr int kFooterHeight = 36;

    explicit TrackHeaderView(bridge::ITrackController* track,
                             bridge::IMeteringProvider* metering,
                             bridge::IArrangementController* arrangement,
                             bridge::IAutomationController* automation,
                             QWidget* parent = nullptr);

    ~TrackHeaderView() override;

    void setTrackList(const std::vector<bridge::TrackUIState>& tracks, const std::map<std::pair<uint64_t, uint32_t>, int>& takesLaneHeights);
    void updateTrackStates(const std::vector<bridge::TrackUIState>& tracks, const std::map<std::pair<uint64_t, uint32_t>, int>& takesLaneHeights);
    void updateMeters(); // queries m_metering directly for each item to avoid allocation
    void updateMeters(const std::vector<bridge::MeterLevel>& levels); // optional vector overload
    void setVerticalOffset(int offsetPx);
    void clearAll();

signals:
    void trackMuteToggled(TrackID id, bool mute);
    void trackSoloToggled(TrackID id, bool solo);
    void trackArmToggled(TrackID id, bool armed);
    void trackInputMonitorToggled(TrackID id, bool enabled);
    void trackRenameRequested(TrackID id, const QString& newName);
    void trackColorChangeRequested(TrackID id, uint32_t colorARGB);
    void trackMoveRequested(TrackID id, uint32_t newIndex, TrackID newParentFolderId);
    void trackDeleteRequested(TrackID id);
    void trackHeightChanged(TrackID id, int newHeight);
    
    void takePromoteRequested(TrackID id, uint32_t laneIndex);
    
    void addAudioTrackRequested();
    void addInstrumentTrackRequested();
    void addFolderTrackRequested();
    
    void insertPluginRequested(TrackID trackId, uint32_t pluginId);
    void insertInstrumentRequested(TrackID trackId, uint32_t pluginId);

    void automationExpansionToggled(TrackID id);
    void takesExpansionToggled(TrackID id);

    /// Emitted during live drag for takes lanes
    void takesLaneHeightChanging(TrackID trackId, uint32_t takeLaneIndex, int newHeight);
    void takesLaneHeightChanged(TrackID trackId, uint32_t takeLaneIndex, int newHeight);

    /// Emitted during live drag to update canvas viewport (lightweight, no reload).
    void automationSubLaneHeightChanging(TrackID trackId, uint32_t subLaneIndex, int newHeight);

    /// Emitted when the user releases the drag — persists to bridge + reloads.
    void automationSubLaneHeightChanged(TrackID trackId, uint32_t subLaneIndex, int newHeight);

    /// Emitted when the user selects "Configure Automation..." from the track context menu.
    void configureAutomationRequested(TrackID id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void drawFolderTrays(QPainter& p);

private:
    void rebuildLayouts();
    void updateAddTrackButtonGeometry();
    void showContextMenu(TrackID trackId, const QPoint& globalPos);
    void onSelectionRequested(TrackID trackId, bool multiSelect, bool rangeSelect);
    int getTrackDepth(TrackID trackId) const;
    void handleTrackDrop(TrackID draggedId, TrackID targetId, DropAction action);
    bool isTrackDescendantOf(TrackID childId, TrackID potentialAncestorId) const;

    // Bridge interfaces
    bridge::ITrackController*  m_track{nullptr};
    bridge::IMeteringProvider* m_metering{nullptr};
    bridge::IArrangementController* m_arrangement{nullptr};
    bridge::IAutomationController*  m_automation{nullptr};
    
    // View state and cached layout
    std::vector<bridge::TrackUIState> m_tracks;
    std::vector<RowLayout> m_layoutGeometries;
    std::map<std::pair<uint64_t, uint32_t>, int> m_takesLaneHeights;
    std::map<std::pair<uint64_t, uint32_t>, int> m_autoSubLaneHeights;
    int m_verticalOffsetPx{0};

    // Resizing state
    int m_resizeRowIndex{-1};
    int m_resizeDragStartY{0};
    int m_resizeDragStartHeight{0};

    // Drag and Drop state
    QPoint m_dragStartPos;
    TrackID m_dropTargetId;
    DropAction m_dropAction{DropAction::None};
    double m_dropIndicatorY{0.0};
    QRectF m_dropRect;

    // Selection tracking
    int m_lastSelectedTrackIndex{-1};

    // Cache of track properties
    std::unordered_map<uint64_t, int> m_trackHeights;
    std::unordered_map<uint64_t, QString> m_trackIcons;
    std::unordered_set<uint64_t> m_groupedTracks;

    // Meter State Cache with Ballistics Filters
    struct TrackMeterState {
        bridge::MeterLevel meter;
        bridge::BallisticsFilter ballistics[2];
        bool initialized{false};
    };
    std::unordered_map<uint64_t, TrackMeterState> m_trackMeters;

    // Hover and Pressed virtual controls tracking
    TrackID m_hoveredTrackId;
    RowType m_hoveredRowType{RowType::Track};
    uint32_t m_hoveredRowIndex{0};
    VirtualControl m_hoveredControl{VirtualControl::None};

    TrackID m_pressedTrackId;
    RowType m_pressedRowType{RowType::Track};
    uint32_t m_pressedRowIndex{0};
    VirtualControl m_pressedControl{VirtualControl::None};

    // Slot Widgets Cache
    std::unordered_map<uint64_t, class InstrumentSlotWidget*> m_instrumentWidgets;
    std::unordered_map<uint64_t, class InputSlotWidget*> m_audioInputWidgets;

    // Shared overlays
    QPushButton* m_addTrackBtn{nullptr};
    QLineEdit*   m_renameEditor{nullptr};
    TrackID      m_renameTrackId;
};

} // namespace presentation::views
