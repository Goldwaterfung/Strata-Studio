// src/Presentation/views/playlist/ProjectPickerScreen.h
#pragma once

#include <QWidget>
#include <QListWidget>
#include <QProgressBar>
#include <QTimer>
#include "project/iproject_lifecycle_controller.h"
#include "browser/ibrowser_controller.h"

namespace presentation::views {

/**
 * @brief Sleek fullscreen overlay that floats on top of the PlaylistWindow.
 *
 * Adheres strictly to Phase 11. Highlights:
 *  - Semi-transparent "glass-panel" neon visual style.
 *  - System file discovery & high-fidelity mock project lists.
 *  - 100ms asynchronous loader polling with double-buffered progress updates.
 *  - Escape-key shortcut integration to close/dismiss the deck overlay.
 */
class ProjectPickerScreen : public QWidget {
    Q_OBJECT
public:
    explicit ProjectPickerScreen(
        bridge::IProjectLifecycleController* lifecycle,
        bridge::IBrowserController*          browser,
        QWidget* parent = nullptr);
    ~ProjectPickerScreen() override = default;

    /**
     * @brief Shows and lifts this widget above all sibling windows, grabbing keyboard focus.
     */
    void showOverlay();

    /**
     * @brief Conceals the dashboard overlay.
     */
    void hideOverlay();

signals:
    /**
     * @brief Signals that a project path was requested to load.
     */
    void projectLoadRequested(const QString& path);

private slots:
    void onProjectClicked(QListWidgetItem* item);
    void onProgressUpdated();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void populateProjectsList();
    void applyAestheticStyle();

private:
    bridge::IProjectLifecycleController* m_lifecycle{nullptr};
    bridge::IBrowserController*          m_browser{nullptr};
    
    QListWidget*  m_projectsList{nullptr};
    QProgressBar* m_progressBar{nullptr};
    QTimer*       m_progressPoller{nullptr};
    
    QString       m_loadingPath;
};

} // namespace presentation::views
