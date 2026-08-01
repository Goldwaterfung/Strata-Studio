// src/Presentation/views/mixer/channel_strip_widget.h
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QString>

#include "cyber_fader.h"
#include "rotary_dial.h"
#include "telemetry_meter.h"
#include "send_slot_container.h"
#include "effect_slot_container.h"
#include "Middle Bridge/tracks/itrack_controller.h"
#include "common/system_primitives.h"
#include <unordered_map>

class QComboBox;

namespace presentation::views {

/**
 * @brief A complete, self-contained mixer channel strip widget.
 *
 * Integrates (top → bottom):
 *  1. Track name header with type color indicator
 *  2. Pre-fader SendSlotContainer
 *  3. RotaryDial  (main pan)
 *  4. Post-fader SendSlotContainer
 *  5. TelemetryMeter + CyberFader (side-by-side)
 *  6. Button row: M (Mute), S (Solo), R (Record Arm), I (Input Monitor)
 *
 * Width: fixed 120 px for track strips, 150 px for master strip.
 * All user actions route exclusively through bridge::ITrackController.
 */
class ChannelStripWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @param isMaster  true = master strip (no sends, no arm/monitor buttons, wider)
     */
    explicit ChannelStripWidget(bool isMaster = false, QWidget* parent = nullptr);
    ~ChannelStripWidget() override = default;

    /**
     * @brief Bind this strip to a specific track in the Middle Bridge.
     */
    void bind(bridge::ITrackController* controller,
              bridge::IAutomationController* automation,
              TrackID trackId,
              const QString& trackName, uint32_t colorARGB);

    /**
     * @brief Refresh all child controls from a fresh TrackUIState snapshot.
     *        Called by the PresentationDirector at 60 Hz.
     */
    void updateFromState(const bridge::TrackUIState& state);

    /**
     * @brief Refresh only the high-frequency dynamic states (fader, pan, mute, solo).
     *        Called by the PresentationDirector at 60 Hz to avoid locking.
     */
    void updateDynamicState(const bridge::TrackDynamicState& state);

    /**
     * @brief Expose the TelemetryMeter so the PresentationDirector can
     *        register it with the metering provider.
     */
    TelemetryMeter* meter() const { return m_meter; }

    TrackID trackId() const { return m_trackId; }
    NodeID channelStripNode() const { return m_channelStripNode; }

    bool isCollapsed() const { return m_isCollapsed; }
    void setCollapsedState(bool collapsed);

    void triggerMuteToggle();
    void triggerSoloToggle();
    void triggerRecordArmToggle();
    void triggerInlineRename();

signals:
    void collapseStateToggled(TrackID trackId, bool collapsed);
    void selectionRequested(TrackID trackId, bool multiSelect, bool rangeSelect);

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QPoint m_dragStartPos;

private Q_SLOTS:
    void onCollapseClicked();
    void onFaderChanged(float value);
    void onDialChanged(float value);
    void onMuteClicked();
    void onSoloClicked();
    void onRecordArmClicked();
    void onInputMonitorClicked();
    void onFaderPressed();
    void onFaderReleased();
    void onDialPressed();
    void onDialReleased();
    void showContextMenu(const QPoint& pos);
    void onInfoClicked();
    void onInputRoutingChanged(int index);
    void onOutputRoutingChanged(int index);

private:
    void buildLayout();

    // --- Bound bridge ---
    bridge::ITrackController* m_controller = nullptr;
    bridge::IAutomationController* m_automation = nullptr;
    TrackID   m_trackId{};
    bool      m_isMaster;
    NodeID    m_channelStripNode{};
    NodeID    m_pannerNode{};

    // --- State mirrored from bridge (used for button toggle rendering) ---
    bool m_muted           = false;
    bool m_soloed          = false;
    bool m_recordArmed     = false;
    bool m_inputMonitoring = false;
    bool m_isSelected      = false;
    uint32_t m_colorARGB   = 0;

    // --- Drag tracking for bulk operations ---
    std::unordered_map<uint64_t, float> m_dragStartTrackLevelsDb;
    std::unordered_map<uint64_t, float> m_dragStartPanPositions;
    float m_dragStartFaderDb = 0.0f;
    float m_dragStartPan = 0.5f;

    // --- Child widgets ---
    QLabel*             m_nameLabel     = nullptr;
    QLabel*             m_typeIndicator = nullptr;  // Small colored dot
    EffectSlotContainer* m_effectSlots   = nullptr;
    SendSlotContainer*  m_preSends      = nullptr;
    RotaryDial*         m_panDial       = nullptr;
    SendSlotContainer*  m_postSends     = nullptr;
    TelemetryMeter*     m_meter         = nullptr;
    CyberFader*         m_fader         = nullptr;
    QComboBox*          m_inputRoutingCombo = nullptr;
    QComboBox*          m_outputRoutingCombo = nullptr;
    QPushButton*        m_infoBtn       = nullptr;

    QPushButton*        m_muteBtn       = nullptr;
    QPushButton*        m_soloBtn       = nullptr;
    QPushButton*        m_recordBtn     = nullptr;
    QPushButton*        m_monitorBtn    = nullptr;
    QPushButton*        m_collapseBtn   = nullptr;
    bool                m_isCollapsed   = false;
    bool                m_isFolder      = false;
};

} // namespace presentation::views
