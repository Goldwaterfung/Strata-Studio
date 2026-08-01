// src/Presentation/views/top_control_panel/transport_controls.cpp
#include "transport_controls.h"
#include "time_display.h"
#include "../theme.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QPainter>

namespace presentation::views {

namespace {

QString getToggleBtnStyle()
{
    return QString(
        "QPushButton {"
        "    color: %1;"
        "    background-color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 4px;"
        "    padding: 4px 6px;"
        "}"
        "QPushButton:hover {"
        "    color: %4;"
        "    background-color: #444444;"
        "    border: 1px solid %5;"
        "}"
        "QPushButton:checked {"
        "    color: %5;"
        "    background-color: %3;"
        "    border: 1px solid %5;"
        "}"
    ).arg(theme::Color::TextMuted.name())
     .arg(theme::Color::BgControl.name())
     .arg(theme::Color::BgSurface.name())
     .arg(theme::Color::TextPrimary.name())
     .arg(theme::Color::AccentGlow.name());
}

QString getTransportBtnStyle()
{
    return QString(
        "QPushButton {"
        "    color: %1;"
        "    background-color: %2;"
        "    border: 1px solid %3;"
        "    border-radius: 4px;"
        "}"
        "QPushButton:hover {"
        "    color: %4;"
        "    background-color: #444444;"
        "    border: 1px solid %5;"
        "}"
    ).arg(theme::Color::TextMuted.name())
     .arg(theme::Color::BgControl.name())
     .arg(theme::Color::BgSurface.name())
     .arg(theme::Color::TextPrimary.name())
     .arg(theme::Color::AccentGlow.name());
}

QPushButton* createSmallButton(const QString& text, const QString& tooltip, QWidget* parent)
{
    auto* btn = new QPushButton(text, parent);
    btn->setToolTip(tooltip);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedSize(44, 36);
    btn->setFont(theme::Font::primary(10, QFont::Bold));
    btn->setStyleSheet(getTransportBtnStyle());
    return btn;
}

} // anonymous namespace

TransportControls::TransportControls(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet("background: transparent; border: none;");
    setupUI();
}

void TransportControls::setupUI()
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(6);

    // --- Loop Toggle ---
    m_loopToggle = new QPushButton(this);
    m_loopToggle->setIcon(theme::PaintHelper::createSvgIcon(":/icons/loop.svg"));
    m_loopToggle->setIconSize(QSize(20, 20));
    m_loopToggle->setCheckable(true);
    m_loopToggle->setToolTip("Toggle timeline looping");
    m_loopToggle->setCursor(Qt::PointingHandCursor);
    m_loopToggle->setFixedSize(44, 36);
    m_loopToggle->setStyleSheet(getToggleBtnStyle());
    connect(m_loopToggle, &QPushButton::clicked, this, &TransportControls::onLoopToggleClicked);
    layout->addWidget(m_loopToggle);

    // --- Separator ---
    auto* sep1 = new QFrame(this);
    sep1->setFrameShape(QFrame::VLine);
    sep1->setStyleSheet(QString("background-color: %1; min-width: 1px; max-width: 1px; border: none;").arg(theme::Color::BgControl.name()));
    layout->addWidget(sep1);

    // --- Transport Buttons: Stop, Play, Record ---
    m_stopBtn = createSmallButton(u8"■", "Stop", this);
    m_playBtn = createSmallButton(u8"▶", "Play / Pause", this);
    m_recordBtn = createSmallButton(u8"●", "Record", this);

    m_stopBtn->setFixedSize(44, 36);
    m_playBtn->setFixedSize(54, 36);
    m_recordBtn->setFixedSize(44, 36);

    connect(m_stopBtn, &QPushButton::clicked, this, &TransportControls::onStopClicked);
    connect(m_playBtn, &QPushButton::clicked, this, &TransportControls::onPlayClicked);
    connect(m_recordBtn, &QPushButton::clicked, this, &TransportControls::onRecordClicked);

    layout->addWidget(m_stopBtn);
    layout->addWidget(m_playBtn);
    layout->addWidget(m_recordBtn);

    // --- Separator ---
    auto* sep2 = new QFrame(this);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setStyleSheet(QString("background-color: %1; min-width: 1px; max-width: 1px; border: none;").arg(theme::Color::BgControl.name()));
    layout->addWidget(sep2);

    // --- Time Display ---
    m_timeDisplay = new TimeDisplay(this);
    layout->addWidget(m_timeDisplay);
}

void TransportControls::bind(bridge::ITimelineController* controller)
{
    m_controller = controller;
    if (m_controller) {
        m_isPlaying = m_controller->isPlaying();
        m_isRecording = m_controller->isRecording();
        m_isRecordArmed = m_controller->isRecordArmed();
        m_isLoopEnabled = m_controller->isLooping();
        m_loopToggle->setChecked(m_isLoopEnabled);
    }
    m_timeDisplay->bind(controller);
}

void TransportControls::updateFromBridge()
{
    if (!m_controller) return;

    // Sync transport state
    bool playing = m_controller->isPlaying();
    bool recording = m_controller->isRecording();

    if (playing != m_isPlaying) {
        m_isPlaying = playing;
        m_playBtn->setStyleSheet(m_isPlaying
            ? QString("QPushButton { color: %1; background-color: #211B35; border: 1px solid %1; border-radius: 4px; }").arg(theme::Color::AccentGlow.name())
            : getTransportBtnStyle());
    }

    // The button state should reflect whether recording is armed (which covers active recording too)
    bool recordArmed = m_controller->isRecordArmed() || recording;

    if (recordArmed != m_isRecordArmed) {
        m_isRecordArmed = recordArmed;
        m_recordBtn->setStyleSheet(m_isRecordArmed
            ? QString("QPushButton { color: %1; background-color: #2A1515; border: 1px solid %1; border-radius: 4px; }").arg(theme::Color::AccentRecord.name())
            : getTransportBtnStyle());
    }    // Sync loop state
    bool looping = m_controller->isLooping();
    if (looping != m_isLoopEnabled) {
        m_isLoopEnabled = looping;
        m_loopToggle->setChecked(m_isLoopEnabled);
    }

    // Sync time display
    uint32_t bar = 1, beat = 1, tick = 0;
    m_controller->getCurrentBBT(bar, beat, tick);
    m_timeDisplay->updatePosition(bar, beat, tick,
                                   m_controller->getCurrentSeconds(),
                                   m_controller->getBPM());
}

void TransportControls::onPlayClicked()
{
    if (m_controller) {
        m_controller->togglePlay();
    }
}

void TransportControls::onStopClicked()
{
    if (m_controller) {
        m_controller->stop();
    }
}

void TransportControls::onRecordClicked()
{
    if (m_controller) {
        m_controller->setRecordArmed(!m_isRecordArmed);
    }
}

void TransportControls::onLoopToggleClicked()
{
    m_isLoopEnabled = !m_isLoopEnabled;
    if (m_controller) {
        m_controller->setLoopEnabled(m_isLoopEnabled);
    }
}



void TransportControls::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw subtle bottom divider
    QColor dividerColor = theme::Color::BgControl;
    dividerColor.setAlphaF(0.5f);
    QPen pen(dividerColor, 1.0);
    painter.setPen(pen);
    double lineY = height() - 1.0;
    painter.drawLine(QPointF(0.0, lineY), QPointF(static_cast<double>(width()), lineY));
}

} // namespace presentation::views
