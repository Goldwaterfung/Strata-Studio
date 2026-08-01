// src/Presentation/views/boot/splash_screen.h
#pragma once

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include "Middle Bridge/boot/iboot_controller.h"

namespace presentation::views {

/**
 * @brief Premium, frameless, cyber-industrial Splash Screen displayed during DAW boot.
 * Monitors the BootController pipeline and updates progress visually with glassmorphic styling.
 */
class SplashScreen : public QWidget, public bridge::IBootController::IListener {
    Q_OBJECT

public:
    explicit SplashScreen(bridge::IBootController* controller, QWidget* parent = nullptr);
    ~SplashScreen() override;

    // === IBootController::IListener Overrides ===
    void onBootStageChanged(bridge::BootStage stage, const std::string& statusText) override;
    void onBootProgressUpdated(float progress) override;
    void onBootCompleted() override;
    void onBootFailed(const std::string& errorMessage) override;

signals:
    void sigStageChanged(int stage, const QString& statusText);
    void sigProgressUpdated(float progress);
    void sigBootCompleted();
    void sigBootFailed(const QString& errorMessage);
    void sigRetryBoot();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private Q_SLOTS:
    void handleStageChanged(int stage, const QString& statusText);
    void handleProgressUpdated(float progress);
    void handleBootFailed(const QString& errorMessage);
    void onRetryClicked();

private:
    void setupUI();

    bridge::IBootController* m_controller{nullptr};
    
    QLabel* m_titleLabel{nullptr};
    QLabel* m_subtitleLabel{nullptr};
    QLabel* m_statusLabel{nullptr};
    QProgressBar* m_progressBar{nullptr};
    QLabel* m_versionLabel{nullptr};
    QPushButton* m_retryButton{nullptr};
};

} // namespace presentation::views
