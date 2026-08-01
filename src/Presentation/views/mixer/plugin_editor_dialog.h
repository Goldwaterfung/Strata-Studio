// src/Presentation/views/mixer/plugin_editor_dialog.h
#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <vector>
#include <QString>
#include "Middle Bridge/tracks/itrack_controller.h"
#include "common/system_primitives.h"
#include "rotary_dial.h"

namespace presentation::views {

/**
 * @brief A premium, custom floating window styled for insert plugin parameter editing.
 */
class PluginEditorDialog : public QDialog {
    Q_OBJECT

public:
    PluginEditorDialog(bridge::ITrackController* controller, 
                       TrackID trackId, 
                       uint32_t slotIndex, 
                       const QString& pluginName, 
                       uint8_t category,
                       QWidget* parent = nullptr,
                       bool isInstrument = false);
    ~PluginEditorDialog() override;

protected:
    void paintEvent(QPaintEvent* event) override;

private Q_SLOTS:
    void onBypassToggled();
    void onRemoveClicked();
    void onKnobChanged(float val);
    void onSidechainSourceChanged(int index);
    void onSidechainGainChanged(float val);
    void onAddAutomationClicked();

private:
    void buildUI();
    void buildLastTweakedHeader(QVBoxLayout* mainLayout);
    void buildSidechainHeader(QVBoxLayout* mainLayout);
    void updateBypassStyle();

    bridge::ITrackController* m_controller = nullptr;
    TrackID                   m_trackId{};
    uint32_t                  m_slotIndex = 0;
    QString                   m_pluginName;
    uint8_t                   m_category = 0;
    bool                      m_isInstrument = false;

    bool                      m_bypassed = false;
    bool                      m_hasNativeEditor = false;

    // UI elements
    QLabel*                   m_titleLabel = nullptr;
    QPushButton*              m_bypassBtn = nullptr;
    QPushButton*              m_removeBtn = nullptr;
    QWidget*                  m_lastTweakedContainer = nullptr;
    QLabel*                   m_lastTweakedLabel = nullptr;
    QPushButton*              m_addAutomationBtn = nullptr;
    QWidget*                  m_editorContent = nullptr;
    std::vector<RotaryDial*>  m_dials;
    std::vector<QLabel*>      m_dialLabels;

    // Sidechain UI elements
    QWidget*                  m_sidechainContainer = nullptr;
    QComboBox*                m_sidechainCombo = nullptr;
    RotaryDial*               m_sidechainGainDial = nullptr;
    QLabel*                   m_sidechainGainLabel = nullptr;
};

} // namespace presentation::views

