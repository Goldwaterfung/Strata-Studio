// src/Presentation/views/playlist/dialogs/MergeArrangementDialog.h
#pragma once

#include <QDialog>
#include <QWidget>
#include <QComboBox>
#include "common/system_primitives.h"
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QPushButton>
#include "theme.h"

namespace bridge {
class IArrangementManagerController;
}

namespace presentation::views {

/**
 * @brief Modal dialog to select merge options when joining two arrangements/scenes.
 * Provides rich checkboxes and layout styled according to the Cyber-Industrial aesthetic.
 */
class MergeArrangementDialog : public QDialog {
    Q_OBJECT
public:
    explicit MergeArrangementDialog(bridge::IArrangementManagerController* managerCtrl, QWidget* parent = nullptr);
    ~MergeArrangementDialog() override = default;

    // Selection accessors
    ArrangementID getSourceArrangementId() const;
    ArrangementID getDestinationArrangementId() const;
    int getMergeMode() const; // 0: Append, 1: Overlay, 2: New Tracks
    bool getImportAudio() const;
    bool getImportMIDI() const;
    bool getImportAutomation() const;
    bool getImportMixerSettings() const;
    bool getLimitToLoop() const;

private:
    void setupUI();
    void applyThemeStyle();

private:
    bridge::IArrangementManagerController* m_managerCtrl{nullptr};
    ArrangementID m_activeId{0xFFFFFFFFu};
    QComboBox*    m_sourceCombo{nullptr};
    
    QRadioButton* m_appendRadio{nullptr};
    QRadioButton* m_overlayRadio{nullptr};
    QRadioButton* m_newTracksRadio{nullptr};
    QButtonGroup* m_modeGroup{nullptr};

    QCheckBox*    m_audioCheck{nullptr};
    QCheckBox*    m_midiCheck{nullptr};
    QCheckBox*    m_autoCheck{nullptr};
    QCheckBox*    m_mixerCheck{nullptr};
    QCheckBox*    m_limitToLoopCheck{nullptr};

    QDialogButtonBox* m_buttonBox{nullptr};
};

} // namespace presentation::views
