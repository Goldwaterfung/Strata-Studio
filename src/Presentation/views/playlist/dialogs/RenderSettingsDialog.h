// src/Presentation/views/playlist/dialogs/RenderSettingsDialog.h
#pragma once

#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QStackedWidget>
#include <QProgressBar>
#include <QLabel>
#include <QTimer>
#include "theme.h"

namespace bridge {
class IRenderController;
class ITimelineController;
class IArrangementController;
}

namespace presentation::views {

/**
 * @brief Modal configuration and progress dialog for offline bouncing & exporting.
 * Contains premium options matching professional DAW capabilities and a simulated
 * high-fidelity asynchronous rendering state.
 */
class RenderSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit RenderSettingsDialog(
        bridge::IRenderController* renderCtrl,
        bridge::ITimelineController* timelineCtrl,
        bridge::IArrangementController* arrangementCtrl,
        QWidget* parent = nullptr
    );
    ~RenderSettingsDialog() override = default;

    // Config options
    QString getFileFormat() const;
    QString getSampleRate() const;
    QString getBitDepth() const;
    bool getEnableDither() const;
    bool getSplitPlanar() const;
    int getRenderRange() const; // 0: Full Song, 1: Loop Region

private slots:
    void startRenderSimulation();
    void onSimulationTick();
    void cancelRender();

private:
    void setupUI();
    void applyThemeStyle();

private:
    // Core Layout Panels (Stacked)
    QStackedWidget* m_pagesStack{nullptr};
    
    // Page 0: Settings Panel Widgets
    QComboBox*    m_formatCombo{nullptr};
    QComboBox*    m_sampleRateCombo{nullptr};
    QComboBox*    m_bitDepthCombo{nullptr};
    QCheckBox*    m_ditherCheck{nullptr};
    QCheckBox*    m_splitPlanarCheck{nullptr};
    
    QRadioButton* m_fullSongRadio{nullptr};
    QRadioButton* m_loopRegionRadio{nullptr};
    QButtonGroup* m_rangeGroup{nullptr};

    QPushButton*  m_btnRender{nullptr};
    QPushButton*  m_btnCancel{nullptr};

    // Page 1: Progress Simulation Panel Widgets
    QLabel*       m_statusLabel{nullptr};
    QProgressBar* m_progressBar{nullptr};
    QPushButton*  m_btnCancelProgress{nullptr};

    // Simulation logic variables
    QTimer* m_simTimer{nullptr};
    int     m_simProgress{0};

    bridge::IRenderController* m_renderCtrl{nullptr};
    bridge::ITimelineController* m_timelineCtrl{nullptr};
    bridge::IArrangementController* m_arrangementCtrl{nullptr};
};

} // namespace presentation::views
