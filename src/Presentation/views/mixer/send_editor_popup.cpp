// src/Presentation/views/mixer/send_editor_popup.cpp
#include "send_editor_popup.h"
#include "../theme.h"
#include "common/math/gain.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QStyle>
#include <cmath>
#include <algorithm>

namespace presentation::views {

SendEditorPopup::SendEditorPopup(bridge::ITrackController* controller,
                                 TrackID trackId,
                                 bool isPreFader,
                                 uint32_t slotIndex,
                                 QWidget* parent)
    : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
    , m_controller(controller)
    , m_trackId(trackId)
    , m_isPreFader(isPreFader)
    , m_slotIndex(slotIndex)
{
    setFixedSize(220, 150);
    setAttribute(Qt::WA_TranslucentBackground, true);

    loadInitialState();
    buildUI();
}

void SendEditorPopup::loadInitialState() {
    if (!m_controller) return;

    auto trackState = m_controller->getTrackState(m_trackId);
    if (m_isPreFader) {
        if (m_slotIndex < trackState.activePreFaderSendCount) {
            const auto& slot = trackState.preFaderSends[m_slotIndex];
            m_cachedGainLinear = Math::Gain::dBToCoeff(slot.leveldB);
            m_cachedPan = slot.panPosition;
            m_cachedEnabled = slot.isEnabled;
        }
    } else {
        if (m_slotIndex < trackState.activePostFaderSendCount) {
            const auto& slot = trackState.postFaderSends[m_slotIndex];
            m_cachedGainLinear = Math::Gain::dBToCoeff(slot.leveldB);
            m_cachedPan = slot.panPosition;
            m_cachedEnabled = slot.isEnabled;
        }
    }
}

void SendEditorPopup::buildUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(8);

    // 1. Header label
    QString destName = "-- Empty --";
    auto trackState = m_controller ? m_controller->getTrackState(m_trackId) : bridge::TrackUIState{};
    if (m_isPreFader && m_slotIndex < trackState.activePreFaderSendCount) {
        destName = QString::fromUtf8(trackState.preFaderSends[m_slotIndex].destinationName);
    } else if (!m_isPreFader && m_slotIndex < trackState.activePostFaderSendCount) {
        destName = QString::fromUtf8(trackState.postFaderSends[m_slotIndex].destinationName);
    }

    m_headerLabel = new QLabel(QString("SEND: %1").arg(destName.toUpper()), this);
    m_headerLabel->setObjectName("sendPopupHeader");
    m_headerLabel->setFont(theme::Font::primary(9, QFont::Bold, 112.0));
    m_headerLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    mainLayout->addWidget(m_headerLabel);

    // 2. Dial Layout (Level & Pan)
    auto* dialsLayout = new QHBoxLayout();
    dialsLayout->setSpacing(16);

    // -- Level Dial --
    auto* levelContainer = new QWidget(this);
    auto* levelLayout = new QVBoxLayout(levelContainer);
    levelLayout->setContentsMargins(0, 0, 0, 0);
    levelLayout->setSpacing(2);

    auto* levelTitle = new QLabel("LEVEL", levelContainer);
    levelTitle->setObjectName("sendPopupTitle");
    levelTitle->setFont(theme::Font::primary(8, QFont::Bold, 112.0));
    levelTitle->setAlignment(Qt::AlignCenter);

    m_levelDial = new RotaryDial(levelContainer);
    m_levelDial->setFixedSize(44, 44);

    // Convert cached linear gain to dB, then normalize to [0, 1]
    float currentDB = std::max(k_minDb, Math::Gain::coeffTodB(m_cachedGainLinear));
    float normLevel = (currentDB - k_minDb) / (k_maxDb - k_minDb);
    m_levelDial->setValue(normLevel);
    m_levelDial->setDefaultValue((0.0f - k_minDb) / (k_maxDb - k_minDb)); // 0 dB fader unity default

    m_levelValLabel = new QLabel(levelContainer);
    m_levelValLabel->setObjectName("sendPopupValue");
    m_levelValLabel->setFont(theme::Font::monospace(8, QFont::Normal));
    m_levelValLabel->setAlignment(Qt::AlignCenter);

    if (currentDB <= k_minDb + 0.5f) {
        m_levelValLabel->setText("-inf dB");
    } else {
        m_levelValLabel->setText(QString::number(static_cast<double>(currentDB), 'f', 1) + " dB");
    }

    levelLayout->addWidget(levelTitle, 0, Qt::AlignCenter);
    levelLayout->addWidget(m_levelDial, 0, Qt::AlignCenter);
    levelLayout->addWidget(m_levelValLabel, 0, Qt::AlignCenter);
    dialsLayout->addWidget(levelContainer);

    // -- Pan Dial --
    auto* panContainer = new QWidget(this);
    auto* panLayout = new QVBoxLayout(panContainer);
    panLayout->setContentsMargins(0, 0, 0, 0);
    panLayout->setSpacing(2);

    auto* panTitle = new QLabel("PAN", panContainer);
    panTitle->setObjectName("sendPopupTitle");
    panTitle->setFont(theme::Font::primary(8, QFont::Bold, 112.0));
    panTitle->setAlignment(Qt::AlignCenter);

    m_panDial = new RotaryDial(panContainer);
    m_panDial->setFixedSize(44, 44);
    m_panDial->setValue(m_cachedPan);
    m_panDial->setDefaultValue(0.5f); // Center default

    m_panValLabel = new QLabel(panContainer);
    m_panValLabel->setObjectName("sendPopupValue");
    m_panValLabel->setFont(theme::Font::monospace(8, QFont::Normal));
    m_panValLabel->setAlignment(Qt::AlignCenter);

    if (m_isPreFader) {
        int panInt = static_cast<int>(std::round((m_cachedPan - 0.5f) * 200.0f));
        if (panInt == 0) {
            m_panValLabel->setText("C");
        } else if (panInt < 0) {
            m_panValLabel->setText(QString("%1% L").arg(std::abs(panInt)));
        } else {
            m_panValLabel->setText(QString("%1% R").arg(panInt));
        }
    } else {
        m_panDial->setEnabled(false);
        m_panDial->setValue(0.5f);
        m_panValLabel->setText("Follows Ch");
        m_panValLabel->setObjectName("sendPopupValueDisabled");
    }

    panLayout->addWidget(panTitle, 0, Qt::AlignCenter);
    panLayout->addWidget(m_panDial, 0, Qt::AlignCenter);
    panLayout->addWidget(m_panValLabel, 0, Qt::AlignCenter);
    dialsLayout->addWidget(panContainer);

    mainLayout->addLayout(dialsLayout);

    // 3. Bypass controls at the bottom
    m_bypassCheckbox = new QCheckBox("Active Send", this);
    m_bypassCheckbox->setFont(theme::Font::primary(8, QFont::Normal));
    m_bypassCheckbox->setChecked(m_cachedEnabled);
    mainLayout->addWidget(m_bypassCheckbox, 0, Qt::AlignCenter);

    // Connections
    connect(m_levelDial, &RotaryDial::valueChanged, this, &SendEditorPopup::onLevelDialChanged);
    connect(m_panDial, &RotaryDial::valueChanged, this, &SendEditorPopup::onPanDialChanged);
    connect(m_bypassCheckbox, &QCheckBox::toggled, this, &SendEditorPopup::onBypassToggled);
}

void SendEditorPopup::onLevelDialChanged(float value) {
    float dB = k_minDb + value * (k_maxDb - k_minDb);
    float gainLinear = (dB <= k_minDb + 0.1f) ? 0.0f : Math::Gain::dBToCoeff(dB);

    if (dB <= k_minDb + 0.5f) {
        m_levelValLabel->setText("-inf dB");
    } else {
        m_levelValLabel->setText(QString::number(static_cast<double>(dB), 'f', 1) + " dB");
    }

    m_cachedGainLinear = gainLinear;

    if (m_controller) {
        m_controller->setSendGain(m_trackId, m_isPreFader, m_slotIndex, gainLinear);
    }
}

void SendEditorPopup::onPanDialChanged(float value) {
    if (!m_isPreFader) return;

    m_cachedPan = value;

    int panInt = static_cast<int>(std::round((value - 0.5f) * 200.0f));
    if (panInt == 0) {
        m_panValLabel->setText("C");
    } else if (panInt < 0) {
        m_panValLabel->setText(QString("%1% L").arg(std::abs(panInt)));
    } else {
        m_panValLabel->setText(QString("%1% R").arg(panInt));
    }

    if (m_controller) {
        m_controller->setSendPan(m_trackId, m_isPreFader, m_slotIndex, value);
    }
}

void SendEditorPopup::onBypassToggled(bool checked) {
    m_cachedEnabled = checked;
    if (m_controller) {
        m_controller->setSendEnabled(m_trackId, m_isPreFader, m_slotIndex, checked);
    }
}

void SendEditorPopup::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    double w = static_cast<double>(width());
    double h = static_cast<double>(height());

    // Paint premium glass panel background
    theme::PaintHelper::drawGlassPanel(&painter, QRectF(0.0, 0.0, w, h), theme::Color::BgBase, 6.0);

    // Draw high contrast outer border
    painter.setPen(QPen(theme::Color::BorderSlotBypassed, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(0.5, 0.5, w - 1.0, h - 1.0), 6.0, 6.0);
}

} // namespace presentation::views
