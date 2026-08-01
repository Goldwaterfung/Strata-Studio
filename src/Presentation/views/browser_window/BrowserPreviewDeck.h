// src/Presentation/views/browser_window/BrowserPreviewDeck.h
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QPushButton>
#include <QTimer>
#include "Middle Bridge/browser/ibrowser_controller.h"

namespace presentation::views {

class PreviewProgressBar : public QWidget {
    Q_OBJECT
public:
    explicit PreviewProgressBar(QWidget* parent = nullptr);

    void setProgress(double progress);
    void setPreviewing(bool previewing);
    void setReady(bool ready);

signals:
    void progressClicked(double ratio);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    double m_progress{0.0};
    bool m_previewing{false};
    bool m_ready{false};
};

class BrowserPreviewDeck : public QWidget {
    Q_OBJECT

public:
    explicit BrowserPreviewDeck(bridge::IBrowserController* controller, QWidget* parent = nullptr);
    ~BrowserPreviewDeck() override = default;

    /**
     * @brief Set the active preview file ID and start updating playhead timeline.
     */
    void setSelectedMedia(MediaID mediaId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void onPlayToggleClicked();
    void onLoopToggled(bool checked);
    void onTick();

private:
    void setupUI();

    bridge::IBrowserController* m_controller{nullptr};
    MediaID m_currentMedia{MediaID::invalid()};

    QPushButton* m_btnPlayToggle{nullptr};
    QPushButton* m_btnLoop{nullptr};
    PreviewProgressBar* m_progressBar{nullptr};
    QTimer* m_timer{nullptr};
};

} // namespace presentation::views
