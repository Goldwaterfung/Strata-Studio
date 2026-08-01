// src/Presentation/views/browser_window/BrowserPreviewDeck.cpp
#include "BrowserPreviewDeck.h"
#include "../theme.h"
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>

namespace presentation::views {

BrowserPreviewDeck::BrowserPreviewDeck(bridge::IBrowserController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller) {
    setupUI();
    
    // Timer polling at 60Hz (16ms)
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &BrowserPreviewDeck::onTick);
    m_timer->start(16);
}

PreviewProgressBar::PreviewProgressBar(QWidget* parent)
    : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void PreviewProgressBar::setProgress(double progress) {
    m_progress = progress;
    update();
}

void PreviewProgressBar::setPreviewing(bool previewing) {
    m_previewing = previewing;
    update();
}

void PreviewProgressBar::setReady(bool ready) {
    m_ready = ready;
    update();
}

void PreviewProgressBar::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    double trackX = 0.0;
    double trackY = 0.0;
    double trackWidth = static_cast<double>(width());
    double trackHeight = static_cast<double>(height());

    if (trackWidth <= 0.0) return;

    QRectF trackRect(trackX, trackY, trackWidth, trackHeight);

    // Draw cyber-glass panel base
    theme::PaintHelper::drawGlassPanel(&painter, trackRect, theme::Color::BgSurface, 4.0);

    // Draw faint background schematic grids inside the tracker
    QColor gridColor = theme::Color::BgControl;
    gridColor.setAlphaF(0.2f); // Explicit float literal
    painter.setPen(QPen(gridColor, 1.0));
    
    double colWidth = trackWidth / 8.0;
    for (int i = 1; i < 8; ++i) {
        double colX = trackX + static_cast<double>(i) * colWidth;
        painter.drawLine(QPointF(colX, trackY), QPointF(colX, trackY + trackHeight));
    }

    // 3. Draw high-performance visual representation of playhead
    double playheadX = trackX + (m_progress * trackWidth);
    
    // Draw active glowing path
    if (m_progress > 0.0) {
        QRectF fillRect(trackX + 1.0, trackY + 1.0, (m_progress * trackWidth) - 1.0, trackHeight - 2.0);
        QColor fillGlow = theme::Color::AccentAudio;
        fillGlow.setAlphaF(0.25f); // Explicit float literal
        painter.setBrush(fillGlow);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(fillRect, 2.0, 2.0);
    }

    // Draw moving progress line & radial neon light on playhead position
    if (m_previewing) {
        QPen playheadPen(theme::Color::AccentGlow, 1.5);
        painter.setPen(playheadPen);
        painter.drawLine(QPointF(playheadX, trackY + 1.0), QPointF(playheadX, trackY + trackHeight - 1.0));

        // Volumetric radial glow around the needle position
        QRectF glowRect(playheadX - 8.0, trackY + (trackHeight / 2.0) - 8.0, 16.0, 16.0);
        theme::PaintHelper::drawVolumetricGlow(&painter, glowRect, theme::Color::AccentGlow, 0.45);
    } else {
        // Draw inactive helper label
        painter.setFont(theme::Font::primary(theme::Font::SizeDetail));
        painter.setPen(theme::Color::TextMuted);
        
        QString statusText = m_ready ? "Ready to Preview" : "Select an audio asset";
        painter.drawText(trackRect, Qt::AlignCenter, statusText);
    }
}

void PreviewProgressBar::mousePressEvent(QMouseEvent* event) {
    if (width() > 0) {
        double clickRatio = static_cast<double>(event->position().x()) / width();
        clickRatio = qBound(0.0, clickRatio, 1.0);
        emit progressClicked(clickRatio);
    }
    QWidget::mousePressEvent(event);
}

void BrowserPreviewDeck::setupUI() {
    setFixedHeight(60);
    setStyleSheet("background: transparent; border: none;");

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // Play/Stop Button
    m_btnPlayToggle = new QPushButton(this);
    m_btnPlayToggle->setFont(theme::Font::primary(9, QFont::Bold));
    m_btnPlayToggle->setText("PLAY");
    m_btnPlayToggle->setFixedSize(60, 44);
    m_btnPlayToggle->setCursor(Qt::PointingHandCursor);
    m_btnPlayToggle->setProperty("state", "preview-stopped");
    layout->addWidget(m_btnPlayToggle);

    // Loop Switch
    m_btnLoop = new QPushButton("LOOP", this);
    m_btnLoop->setCheckable(true);
    m_btnLoop->setFont(theme::Font::primary(theme::Font::SizeDetail, QFont::Bold));
    m_btnLoop->setFixedSize(52, 44);
    m_btnLoop->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_btnLoop);

    // Add layout stretch to isolate custom progress track on the right
    layout->addSpacing(10);
    
    m_progressBar = new PreviewProgressBar(this);
    layout->addWidget(m_progressBar);

    // Connect interactions
    connect(m_btnPlayToggle, &QPushButton::clicked, this, &BrowserPreviewDeck::onPlayToggleClicked);
    connect(m_btnLoop, &QPushButton::toggled, this, &BrowserPreviewDeck::onLoopToggled);
    connect(m_progressBar, &PreviewProgressBar::progressClicked, this, [this](double ratio) {
        if (m_controller && m_currentMedia.isValid()) {
            bool loop = m_btnLoop->isChecked();
            m_controller->startAudioPreview(m_currentMedia, loop, static_cast<float>(ratio));
            update();
        }
    });
}

void BrowserPreviewDeck::setSelectedMedia(MediaID mediaId) {
    m_currentMedia = mediaId;
    m_progressBar->setReady(mediaId.isValid());
    m_progressBar->setProgress(0.0);
    update();
}

void BrowserPreviewDeck::onPlayToggleClicked() {
    if (!m_controller || !m_currentMedia.isValid()) return;

    if (m_controller->isPreviewing()) {
        m_controller->stopAudioPreview();
    } else {
        bool loop = m_btnLoop->isChecked();
        m_controller->startAudioPreview(m_currentMedia, loop);
    }
    update();
}

void BrowserPreviewDeck::onLoopToggled(bool checked) {
    if (!m_controller || !m_currentMedia.isValid()) return;

    // If already previewing, restart with new loop setting
    if (m_controller->isPreviewing()) {
        m_controller->startAudioPreview(m_currentMedia, checked);
    }
}

void BrowserPreviewDeck::onTick() {
    if (!m_controller) return;

    bool isPreviewing = m_controller->isPreviewing();
    
    // Update button text and color state dynamically
    if (isPreviewing) {
        m_btnPlayToggle->setText("STOP");
        if (m_btnPlayToggle->property("state").toString() != "preview-playing") {
            m_btnPlayToggle->setProperty("state", "preview-playing");
            theme::Style::updateDynamic(m_btnPlayToggle);
        }
        m_progressBar->setPreviewing(true);
        m_progressBar->setProgress(static_cast<double>(m_controller->getPreviewPlayhead()));
    } else {
        m_btnPlayToggle->setText("PLAY");
        if (m_btnPlayToggle->property("state").toString() != "preview-stopped") {
            m_btnPlayToggle->setProperty("state", "preview-stopped");
            theme::Style::updateDynamic(m_btnPlayToggle);
        }
        m_progressBar->setPreviewing(false);
        m_progressBar->setProgress(0.0);
    }
    update();
}

void BrowserPreviewDeck::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 1. Draw sleek frosted top border separating preview deck
    QColor topBorder = theme::Color::BgControl;
    topBorder.setAlphaF(0.4f); // Explicit float literal
    QPen borderPen(topBorder, 1.0);
    painter.setPen(borderPen);
    painter.drawLine(QPointF(0.0, 0.0), QPointF(static_cast<double>(width()), 0.0));
}

void BrowserPreviewDeck::mousePressEvent(QMouseEvent* event) {
    QWidget::mousePressEvent(event);
}

} // namespace presentation::views
