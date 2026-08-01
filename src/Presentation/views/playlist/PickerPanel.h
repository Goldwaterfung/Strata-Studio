// src/Presentation/views/playlist/PickerPanel.h
#pragma once

#include <QWidget>
#include <QListWidget>
#include <QButtonGroup>
#include <QPushButton>
#include <QStackedWidget>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QApplication>
#include <QPainter>
#include "browser/ibrowser_controller.h"
#include "telemetry/ipattern_data_provider.h"
#include "../theme.h"

namespace presentation::views {

/**
 * @brief High-performance, premium subclass of QListWidget that handles drag events.
 * It builds responsive neon drag thumbnails dynamically on mouse moves.
 */
class PickerListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit PickerListWidget(QWidget* parent = nullptr);

signals:
    void itemDragStarted(const QString& name, int itemType, uint64_t mediaIdRaw);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QPoint m_dragStartPos;
};

/**
 * @brief The PickerPanel browser sidebar for the DAW's playlist view.
 * 
 * Provides quick selection and drag-and-drop of audio samples, automatable parameters,
 * and MIDI patterns into the arrangement grid. Adheres to the 7-layer hierarchy,
 * talking exclusively to bridge controllers on the GUI thread.
 */
class PickerPanel : public QWidget {
    Q_OBJECT
public:
    explicit PickerPanel(bridge::IBrowserController* browser,
                         bridge::IPatternDataProvider* patternData,
                         QWidget* parent = nullptr);
    ~PickerPanel() override = default;

signals:
    void clipDragStarted(MediaID mediaId, int itemType);

private slots:
    void onTabClicked(int id);
    void handleItemDrag(const QString& name, int type, uint64_t mediaIdRaw);

private:
    void setupUI();
    void applyThemeStyle();
    void reloadAudio();
    void reloadAutomation();
    void reloadPatterns();

private:
    bridge::IBrowserController*   m_browser{nullptr};
    bridge::IPatternDataProvider* m_patternData{nullptr};

    // UI elements
    QButtonGroup*   m_tabGroup{nullptr};
    QPushButton*    m_audioTab{nullptr};
    QPushButton*    m_autoTab{nullptr};
    QPushButton*    m_patternTab{nullptr};
    QStackedWidget* m_stackedWidget{nullptr};

    PickerListWidget* m_audioList{nullptr};
    PickerListWidget* m_automationList{nullptr};
    PickerListWidget* m_patternList{nullptr};
};

} // namespace presentation::views
