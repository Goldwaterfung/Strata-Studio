#include "splash_screen.h"
#include <project_config.h>
#include "Presentation/views/theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>
#include <QApplication>
#include <QPushButton>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QScreen>

namespace presentation::views {

SplashScreen::SplashScreen(bridge::IBootController* controller, QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_controller(controller)
{
    // High-DPI Geometry Scaling: scale fixed sizes dynamically based on logical DPI
    qreal dpiScale = 1.0;
    if (QScreen* scr = screen()) {
        dpiScale = scr->logicalDotsPerInch() / 96.0;
    }
    if (dpiScale < 1.0) {
        dpiScale = 1.0;
    }
    setFixedSize(static_cast<int>(540.0 * dpiScale), static_cast<int>(340.0 * dpiScale));
    setAttribute(Qt::WA_TranslucentBackground);

    setupUI();

    // Wire local thread-safe queued slots
    connect(this, &SplashScreen::sigStageChanged, this, &SplashScreen::handleStageChanged);
    connect(this, &SplashScreen::sigProgressUpdated, this, &SplashScreen::handleProgressUpdated);
    connect(this, &SplashScreen::sigBootFailed, this, &SplashScreen::handleBootFailed);

    if (m_controller) {
        auto stage = m_controller->getCurrentStage();
        float progress = m_controller->getProgress();
        std::string status = m_controller->getStatusText();
        
        handleStageChanged(static_cast<int>(stage), QString::fromStdString(status));
        handleProgressUpdated(progress);
        
        if (stage == bridge::BootStage::FAILED) {
            handleBootFailed(QString::fromStdString(m_controller->getErrorMessage()));
        }
        
        m_controller->registerListener(this);
    }
}

SplashScreen::~SplashScreen() {
    if (m_controller) {
        m_controller->unregisterListener(this);
    }
}

void SplashScreen::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        close();
    }
    QWidget::keyPressEvent(event);
}

void SplashScreen::closeEvent(QCloseEvent* event) {
    if (m_controller) {
        auto stage = m_controller->getCurrentStage();
        if (stage != bridge::BootStage::COMPLETED && stage != bridge::BootStage::FAILED && stage != bridge::BootStage::IDLE) {
            m_controller->cancelBootSequence();
        }
    }
    event->accept();
}

void SplashScreen::setupUI() {
    // Root layout
    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(30, 40, 30, 30);
    rootLayout->setSpacing(8); // Standardize vertical element spacing

    // Title Widget Group
    m_titleLabel = new QLabel(QString::fromUtf8(config::PROJECT_DISPLAY_NAME.data(), static_cast<qsizetype>(config::PROJECT_DISPLAY_NAME.size())).toUpper(), this);
    m_titleLabel->setFont(theme::Font::primary(22, QFont::Bold));
    m_titleLabel->setStyleSheet(QString("color: %1; letter-spacing: 2px;").arg(theme::Color::AccentGlow.name()));
    m_titleLabel->setAlignment(Qt::AlignCenter);

    m_subtitleLabel = new QLabel("STRATA STUDIO", this);
    m_subtitleLabel->setFont(theme::Font::primary(9, QFont::Bold)); // Bold for better readability
    m_subtitleLabel->setStyleSheet(QString("color: %1; letter-spacing: 5px;").arg(theme::Color::TextPrimary.name())); // Higher contrast
    m_subtitleLabel->setAlignment(Qt::AlignCenter);

    // Volumetric Glow effect for the title (parented to label)
    QGraphicsDropShadowEffect* glowEffect = new QGraphicsDropShadowEffect(m_titleLabel);
    glowEffect->setBlurRadius(15);
    glowEffect->setColor(theme::Color::AccentGlow);
    glowEffect->setOffset(0, 0);
    m_titleLabel->setGraphicsEffect(glowEffect);

    // Status label showing boot stage descriptions
    m_statusLabel = new QLabel("Starting system bootstrap...", this);
    m_statusLabel->setFont(theme::Font::primary(10, QFont::Normal)); // Slightly larger for legibility
    m_statusLabel->setStyleSheet(QString("color: %1;").arg(theme::Color::TextPrimary.name()));
    m_statusLabel->setAlignment(Qt::AlignLeft);

    // Progress bar styled with industrial glassmorphic styling
    m_progressBar = new QProgressBar(this);
    m_progressBar->setFixedHeight(6);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    
    // Custom style sheet for premium cyber progress bar
    m_progressBar->setStyleSheet(QString(
        "QProgressBar {"
        "   background-color: %1;"
        "   border: 1px solid %2;"
        "   border-radius: 3px;"
        "}"
        "QProgressBar::chunk {"
        "   background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %3, stop:1 %4);"
        "   border-radius: 2px;"
        "}"
    ).arg(theme::Color::BgControl.name())
     .arg(theme::Color::BgControl.lighter(110).name())
     .arg(theme::Color::AccentMIDI.name()) // Purple to Cyber-Mint gradient!
     .arg(theme::Color::AccentGlow.name()));

    // Bottom info bar (Version and copyright)
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(0);
    bottomLayout->setContentsMargins(0, 10, 0, 0);

    m_versionLabel = new QLabel("v1.0.0-PROD (C++20 ENGINE)", this);
    m_versionLabel->setFont(theme::Font::monospace(9, QFont::Normal)); // Larger/Heavier for legibility
    m_versionLabel->setStyleSheet(QString("color: %1;").arg(theme::Color::TextPrimary.name())); // Higher contrast

    QLabel* copyrightLabel = new QLabel(QString::fromUtf8(config::PROJECT_DISPLAY_NAME.data(), static_cast<qsizetype>(config::PROJECT_DISPLAY_NAME.size())) + " SYSTEMS INC.", this);
    copyrightLabel->setFont(theme::Font::primary(9, QFont::DemiBold)); // Larger for legibility
    copyrightLabel->setStyleSheet(QString("color: %1;").arg(theme::Color::TextMuted.name()));
    copyrightLabel->setAlignment(Qt::AlignRight);

    bottomLayout->addWidget(m_versionLabel);
    bottomLayout->addWidget(copyrightLabel);

    // Hidden neon cyber retry button
    m_retryButton = new QPushButton("RETRY SYSTEM BOOT", this);
    m_retryButton->setFont(theme::Font::monospace(10, QFont::Bold));
    m_retryButton->setStyleSheet(QString(
        "QPushButton {"
        "   background-color: %1;"
        "   color: %2;"
        "   border: 1px solid %2;"
        "   border-radius: 4px;"
        "   padding: 6px 16px;"
        "}"
        "QPushButton:hover {"
        "   background-color: %2;"
        "   color: %1;"
        "}"
    ).arg(theme::Color::BgAlertMuted.name())
     .arg(theme::Color::BorderAlert.name()));
    m_retryButton->setVisible(false);
    connect(m_retryButton, &QPushButton::clicked, this, &SplashScreen::onRetryClicked);

    // Add elements to root layout with appropriate spacing
    rootLayout->addWidget(m_titleLabel);
    rootLayout->addWidget(m_subtitleLabel);
    rootLayout->addSpacing(10);
    rootLayout->addStretch(1);
    rootLayout->addWidget(m_statusLabel);
    rootLayout->addWidget(m_progressBar);
    rootLayout->addSpacing(5);
    rootLayout->addWidget(m_retryButton, 0, Qt::AlignCenter);
    rootLayout->addSpacing(5);
    rootLayout->addLayout(bottomLayout);
}

void SplashScreen::paintEvent(QPaintEvent* event) {
    (void)event;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw dark rounded glass plate
    QRectF rect(0, 0, width(), height());
    theme::PaintHelper::drawGlassPanel(&painter, rect, theme::Color::BgBase, 12.0);

    // Paint an ultra-subtle tech grid on the splash screen background
    painter.setPen(QPen(theme::Color::GridOverlay, 1.0));
    int gridSpacing = 20;
    for (int x = gridSpacing; x < width(); x += gridSpacing) {
        painter.drawLine(x, 0, x, height());
    }
    for (int y = gridSpacing; y < height(); y += gridSpacing) {
        painter.drawLine(0, y, width(), y);
    }
}

// === IBootController::IListener callbacks (Thread-safe signaling) ===

void SplashScreen::onBootStageChanged(bridge::BootStage stage, const std::string& statusText) {
    emit sigStageChanged(static_cast<int>(stage), QString::fromStdString(statusText));
}

void SplashScreen::onBootProgressUpdated(float progress) {
    emit sigProgressUpdated(progress);
}

void SplashScreen::onBootCompleted() {
    emit sigBootCompleted();
}

void SplashScreen::onBootFailed(const std::string& errorMessage) {
    emit sigBootFailed(QString::fromStdString(errorMessage));
}

// === Private Slots executed on GUI Thread ===

void SplashScreen::handleStageChanged(int stage, const QString& statusText) {
    (void)stage;
    m_statusLabel->setText(statusText);
}

void SplashScreen::handleProgressUpdated(float progress) {
    m_progressBar->setValue(static_cast<int>(progress * 100.0f));
}

void SplashScreen::handleBootFailed(const QString& errorMessage) {
    m_statusLabel->setText("CRITICAL ERR: " + errorMessage);
    m_progressBar->setStyleSheet(QString(
        "QProgressBar {"
        "   background-color: %1;"
        "   border: 1px solid %2;"
        "   border-radius: 3px;"
        "}"
        "QProgressBar::chunk {"
        "   background-color: %3;"
        "   border-radius: 2px;"
        "}"
    ).arg(theme::Color::BgControl.name())
     .arg(theme::Color::BorderAlertMuted.name())
     .arg(theme::Color::AccentRecord.name())); // Turn bar deep red on error

    if (m_retryButton) {
        m_retryButton->setVisible(true);
    }
}

void SplashScreen::onRetryClicked() {
    if (m_retryButton) {
        m_retryButton->setVisible(false);
    }

    // Reset stylesheet to normal
    m_progressBar->setStyleSheet(QString(
        "QProgressBar {"
        "   background-color: %1;"
        "   border: 1px solid %2;"
        "   border-radius: 3px;"
        "}"
        "QProgressBar::chunk {"
        "   background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %3, stop:1 %4);"
        "   border-radius: 2px;"
        "}"
    ).arg(theme::Color::BgControl.name())
     .arg(theme::Color::BgControl.lighter(110).name())
     .arg(theme::Color::AccentMIDI.name())
     .arg(theme::Color::AccentGlow.name()));

    m_progressBar->setValue(0);
    m_statusLabel->setText("Retrying bootstrap sequence...");

    if (m_controller) {
        m_controller->startBootSequence();
    }
    emit sigRetryBoot();
}

} // namespace presentation::views
