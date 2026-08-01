// src/Presentation/views/mixer/send_editor_popup.h
#pragma once

#include <QDialog>
#include <QLabel>
#include <QCheckBox>
#include "rotary_dial.h"
#include "Middle Bridge/tracks/itrack_controller.h"

namespace presentation::views {

class SendEditorPopup : public QDialog {
    Q_OBJECT

public:
    SendEditorPopup(bridge::ITrackController* controller,
                    TrackID trackId,
                    bool isPreFader,
                    uint32_t slotIndex,
                    QWidget* parent = nullptr);
    ~SendEditorPopup() override = default;

protected:
    void paintEvent(QPaintEvent* event) override;

private Q_SLOTS:
    void onLevelDialChanged(float value);
    void onPanDialChanged(float value);
    void onBypassToggled(bool checked);

private:
    void buildUI();
    void loadInitialState();

    bridge::ITrackController* m_controller = nullptr;
    TrackID                   m_trackId;
    bool                      m_isPreFader = false;
    uint32_t                  m_slotIndex = 0;

    QLabel*                   m_headerLabel = nullptr;
    QLabel*                   m_levelValLabel = nullptr;
    QLabel*                   m_panValLabel = nullptr;
    RotaryDial*               m_levelDial = nullptr;
    RotaryDial*               m_panDial = nullptr;
    QCheckBox*                m_bypassCheckbox = nullptr;

    float                     m_cachedGainLinear = 0.0f;
    float                     m_cachedPan = 0.5f;
    bool                      m_cachedEnabled = false;

    static constexpr float k_minDb = -60.0f;
    static constexpr float k_maxDb = 12.0f;
};

} // namespace presentation::views
