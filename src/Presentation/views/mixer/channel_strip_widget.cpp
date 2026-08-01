// src/Presentation/views/mixer/channel_strip_widget.cpp
#include "channel_strip_widget.h"
#include "../theme.h"
#include "common/math/gain.h"

#include <QPainter>
#include <QPen>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QMenu>
#include <QLineEdit>
#include <QColorDialog>
#include <QEvent>
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QDataStream>
#include <QKeyEvent>
#include <QComboBox>
#include "playlist/dialogs/DAWInputDialog.h"
#include <QAbstractItemView>
#include <algorithm>
#include <cmath>
#include <functional>

namespace presentation::views {

class DynamicComboBox : public QComboBox {
public:
    using QComboBox::QComboBox;
    std::function<void()> onAboutToShow;
protected:
    void showPopup() override {
        if (onAboutToShow) onAboutToShow();

        QMenu menu(this);
        menu.setStyleSheet(theme::Style::getGlobalStyleSheet());

        int currentIdx = currentIndex();
        for (int i = 0; i < count(); ++i) {
            QString txt = itemText(i);
            QAction* act = menu.addAction(txt);
            act->setData(itemData(i));
            if (i == currentIdx) {
                act->setCheckable(true);
                act->setChecked(true);
            }
        }

        QPoint pos = mapToGlobal(QPoint(0, height()));
        QAction* chosen = menu.exec(pos);
        if (chosen) {
            for (int i = 0; i < count(); ++i) {
                if (itemText(i) == chosen->text()) {
                    setCurrentIndex(i);
                    Q_EMIT activated(i);
                    break;
                }
            }
        }
    }
};

// ---------------------------------------------------------------------------
// Button style helpers
// ---------------------------------------------------------------------------

// namespace
// Button styling helpers are now fully centralized.

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ChannelStripWidget::ChannelStripWidget(bool isMaster, QWidget* parent)
    : QWidget(parent)
    , m_isMaster(isMaster)
{
    setFixedWidth(isMaster ? theme::Layout::MasterStripWidth : theme::Layout::TrackStripWidth);
    setObjectName(isMaster ? "masterChannelStrip" : "channelStrip");
    setStyleSheet(theme::Style::getGlobalStyleSheet());
    setFocusPolicy(Qt::StrongFocus);
    buildLayout();

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &ChannelStripWidget::showContextMenu);
}

void ChannelStripWidget::buildLayout()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    // ── 1. Header (name + type indicator dot) ────────────────────────────
    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(8);

    m_nameLabel = new QLabel(m_isMaster ? "MASTER" : "TRACK", this);
    m_nameLabel->setObjectName("trackNameLabel");
    m_nameLabel->setFont(theme::Font::monospace(7, QFont::Bold, 115.0));
    m_nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_nameLabel->setFixedHeight(20);
    m_nameLabel->installEventFilter(this);
    header->addWidget(m_nameLabel, 1);

    m_infoBtn = new QPushButton(QString::fromUtf8("ℹ"), this);
    m_infoBtn->setObjectName("infoBtn");
    m_infoBtn->setFixedSize(16, 16);
    m_infoBtn->setFont(theme::Font::primary(8, QFont::Bold));
    header->addWidget(m_infoBtn, 0, Qt::AlignRight | Qt::AlignVCenter);
    connect(m_infoBtn, &QPushButton::clicked, this, &ChannelStripWidget::onInfoClicked);

    m_typeIndicator = new QLabel(this);
    m_typeIndicator->setFixedSize(10, 10);
    QString dotColor = m_isMaster ? "#FF3B30" : "#3B82F6";
    m_typeIndicator->setStyleSheet(
        QString("background-color: %1; border-radius: 5px;").arg(dotColor));
    header->addWidget(m_typeIndicator, 0, Qt::AlignRight | Qt::AlignVCenter);

    if (!m_isMaster) {
        m_collapseBtn = new QPushButton(QString::fromUtf8("▼"), this);
        m_collapseBtn->setObjectName("collapseBtn");
        m_collapseBtn->setFixedSize(18, 18);
        m_collapseBtn->setCheckable(true);
        m_collapseBtn->setChecked(false);
        m_collapseBtn->setFont(theme::Font::primary(9, QFont::Bold));
        m_collapseBtn->setVisible(false);
        header->addWidget(m_collapseBtn, 0, Qt::AlignRight | Qt::AlignVCenter);
        connect(m_collapseBtn, &QPushButton::clicked, this, &ChannelStripWidget::onCollapseClicked);
    }

    root->addLayout(header);

    if (!m_isMaster) {
        auto* inputCombo = new DynamicComboBox(this);
        m_inputRoutingCombo = inputCombo;
        m_inputRoutingCombo->setFixedHeight(22);
        m_inputRoutingCombo->setFont(theme::Font::primary(9));
        m_inputRoutingCombo->setToolTip("Input Routing");
        root->addWidget(m_inputRoutingCombo);

        inputCombo->onAboutToShow = [this, inputCombo]() {
            if (!m_controller) return;
            QSignalBlocker blocker(inputCombo);
            inputCombo->clear();
            inputCombo->addItem("No Input", static_cast<qulonglong>(0xFFFFFFFF));
            
            auto inputs = m_controller->getAvailableTrackInputs(m_trackId);
            for (const auto& input : inputs) {
                uint64_t data = (static_cast<uint64_t>(input.numChannels) << 32) | static_cast<uint64_t>(input.optionId);
                inputCombo->addItem(QString::fromStdString(input.name), static_cast<qulonglong>(data));
            }
            
            auto state = m_controller->getTrackState(m_trackId);
            int activeIdx = 0;
            if (state.trackInput.hasInputSlot) {
                uint32_t activeInput = state.trackInput.mappedPhysicalInputIndex;
                for (int i = 0; i < inputCombo->count(); ++i) {
                    uint64_t data = inputCombo->itemData(i).toULongLong();
                    uint32_t cIdx = static_cast<uint32_t>(data & 0xFFFFFFFF);
                    if (cIdx == activeInput) {
                        activeIdx = i;
                        break;
                    }
                }
            }
            inputCombo->setCurrentIndex(activeIdx);
        };

        connect(m_inputRoutingCombo, qOverload<int>(&QComboBox::activated), this, &ChannelStripWidget::onInputRoutingChanged);
    }

    // ── 1.5. Plugin Insert Slots ───
    m_effectSlots = new EffectSlotContainer(this);
    root->addWidget(m_effectSlots);

    // ── 2. Pre-fader sends (not on master) ───────────────────────────────
    if (!m_isMaster) {
        m_preSends = new SendSlotContainer(true /*isPreFader*/, this);
        root->addWidget(m_preSends);
    }

    // ── 3. Pan dial ───────────────────────────────────────────────────────
    // ── 3. Pan dial ───────────────────────────────────────────────────────
    m_panDial = new RotaryDial(this);
    m_panDial->setDefaultValue(Math::Gain::CENTER_PAN_NORMALIZED);
    m_panDial->resetToDefault();
    m_panDial->setToolTip("Pan");
    m_panDial->setFixedSize(52, 52);
    root->addWidget(m_panDial, 0, Qt::AlignCenter);
    connect(m_panDial, &RotaryDial::valueChanged, this, &ChannelStripWidget::onDialChanged);
    connect(m_panDial, &RotaryDial::controlPressed, this, &ChannelStripWidget::onDialPressed);
    connect(m_panDial, &RotaryDial::controlReleased, this, &ChannelStripWidget::onDialReleased);

    // ── 4. Post-fader sends (not on master) ──────────────────────────────
    if (!m_isMaster) {
        m_postSends = new SendSlotContainer(false /*isPreFader*/, this);
        root->addWidget(m_postSends);
    }

    // ── 5. Level meter + fader (side by side) ────────────────────────────
    auto* meterRow = new QHBoxLayout();
    meterRow->setContentsMargins(0, 0, 0, 0);
    meterRow->setSpacing(8);

    m_meter = new TelemetryMeter(this);
    m_meter->setFixedWidth(44);
    m_meter->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    meterRow->addWidget(m_meter);

    m_fader = new CyberFader(this);
    m_fader->setDefaultValue(Math::Gain::UNITY_NORMALIZED); // 0.0dB unity
    m_fader->resetToDefault();
    m_fader->setToolTip("Volume");
    m_fader->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    meterRow->addWidget(m_fader);

    connect(m_fader, &CyberFader::valueChanged, this, &ChannelStripWidget::onFaderChanged);
    connect(m_fader, &CyberFader::controlPressed, this, &ChannelStripWidget::onFaderPressed);
    connect(m_fader, &CyberFader::controlReleased, this, &ChannelStripWidget::onFaderReleased);

    root->addLayout(meterRow, 1);

    // ── 5.5. Output Routing Combo (before buttons, non-master only) ──────
    if (!m_isMaster) {
        auto* outputCombo = new DynamicComboBox(this);
        m_outputRoutingCombo = outputCombo;
        m_outputRoutingCombo->setFixedHeight(22);
        m_outputRoutingCombo->setFont(theme::Font::primary(9));
        m_outputRoutingCombo->setToolTip("Output Routing");
        root->addWidget(m_outputRoutingCombo);

        outputCombo->onAboutToShow = [this, outputCombo]() {
            if (!m_controller) return;
            QSignalBlocker blocker(outputCombo);
            outputCombo->clear();
            outputCombo->addItem("Master Bus", static_cast<qulonglong>(TrackID::invalid().toRaw()));

            auto tracks = m_controller->getAllTracks();
            for (const auto& t : tracks) {
                if (t.trackId == m_trackId) continue;
                if (t.type == composition::TrackType::AUDIO || t.type == composition::TrackType::INSTRUMENT) continue;
                outputCombo->addItem(QString::fromUtf8(t.name), static_cast<qulonglong>(t.trackId.toRaw()));
            }

            auto state = m_controller->getTrackState(m_trackId);
            int activeIdx = 0;
            for (int i = 0; i < outputCombo->count(); ++i) {
                if (outputCombo->itemData(i).toULongLong() == state.outputTargetTrackId.toRaw()) {
                    activeIdx = i;
                    break;
                }
            }
            outputCombo->setCurrentIndex(activeIdx);
        };

        connect(m_outputRoutingCombo, qOverload<int>(&QComboBox::activated), this, &ChannelStripWidget::onOutputRoutingChanged);
    }

    // ── 6. Button row ─────────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(8);

    auto makeBtn = [&](const QString& label) -> QPushButton* {
        auto* btn = new QPushButton(label, this);
        btn->setFixedHeight(28);
        btn->setCheckable(true);
        btn->setFont(theme::Font::monospace(6, QFont::Bold));
        return btn;
    };

    m_muteBtn = makeBtn("M");
    m_muteBtn->setObjectName(QStringLiteral("muteBtn"));
    m_soloBtn = makeBtn("S");
    m_soloBtn->setObjectName(QStringLiteral("soloBtn"));
    btnRow->addWidget(m_muteBtn);
    btnRow->addWidget(m_soloBtn);

    if (!m_isMaster) {
        m_recordBtn  = makeBtn("R");
        m_recordBtn->setObjectName(QStringLiteral("recordBtn"));
        m_monitorBtn = makeBtn("I");
        m_monitorBtn->setObjectName(QStringLiteral("monitorBtn"));
        btnRow->addWidget(m_recordBtn);
        btnRow->addWidget(m_monitorBtn);
    }

    root->addLayout(btnRow);

    // Connect buttons
    connect(m_muteBtn, &QPushButton::clicked, this, &ChannelStripWidget::onMuteClicked);
    connect(m_soloBtn, &QPushButton::clicked, this, &ChannelStripWidget::onSoloClicked);
    if (m_recordBtn)  connect(m_recordBtn,  &QPushButton::clicked, this, &ChannelStripWidget::onRecordArmClicked);
    if (m_monitorBtn) connect(m_monitorBtn, &QPushButton::clicked, this, &ChannelStripWidget::onInputMonitorClicked);
}

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

void ChannelStripWidget::bind(bridge::ITrackController* controller,
                               bridge::IAutomationController* automation,
                               TrackID trackId,
                               const QString& trackName,
                               uint32_t colorARGB)
{
    m_controller = controller;
    m_automation = automation;
    m_trackId    = trackId;
    m_nameLabel->setText(trackName.toUpper().left(8));

    m_colorARGB = colorARGB;
    m_isCollapsed = false;
    if (m_collapseBtn) {
        m_collapseBtn->setChecked(false);
        m_collapseBtn->setText(QString::fromUtf8("▼"));
        m_collapseBtn->setVisible(false);
    }

    if (m_controller && !m_isMaster) {
        bridge::TrackUIState tState = m_controller->getTrackState(m_trackId);
        const bool isFolder = (tState.type == composition::TrackType::FOLDER);
        m_isFolder = isFolder;
        if (m_collapseBtn) {
            m_collapseBtn->setVisible(isFolder);
        }
        if (isFolder) {
            if (m_typeIndicator) {
                m_typeIndicator->setText(QString::fromUtf8("📂"));
                m_typeIndicator->setFont(theme::Font::primary(7));
                m_typeIndicator->setStyleSheet("color: #9DB2BF; background: transparent; border: none;");
                m_typeIndicator->setFixedSize(14, 14);
            }
            setStyleSheet(theme::Style::getGlobalStyleSheet());
        } else {
            if (m_typeIndicator) {
                m_typeIndicator->setText(QString());
                m_typeIndicator->setFixedSize(10, 10);
                QColor c = QColor::fromRgba(m_colorARGB);
                m_typeIndicator->setStyleSheet(
                    QString("background-color: %1; border-radius: 5px;").arg(c.name(QColor::HexArgb)));
            }
            setStyleSheet(theme::Style::getGlobalStyleSheet());
        }
    } else {
        setStyleSheet(theme::Style::getGlobalStyleSheet());
    }

    if (m_isMaster && controller) {
        m_channelStripNode = controller->getMasterChannelStripNode();
    }

    if (m_effectSlots)     m_effectSlots->bind(controller, trackId);
    if (m_preSends)        m_preSends->bind(controller, trackId);
    if (m_postSends)       m_postSends->bind(controller, trackId);
}

void ChannelStripWidget::updateDynamicState(const bridge::TrackDynamicState& state)
{
    // Update fader (normalize gain coefficient back to 0-1)
    if (!m_fader->isDragging()) {
        float gainLinear = Math::Gain::dBToCoeff(state.faderLeveldB);
        float norm       = Math::Gain::linearToNormalized(gainLinear);
        if (std::abs(m_fader->value() - norm) > 0.002f) {
            QSignalBlocker blk(m_fader);
            m_fader->setValue(norm);
        }
    }

    // Update pan dial
    if (!m_panDial->isDragging()) {
        float pan = state.panPosition;
        if (std::abs(m_panDial->value() - pan) > 0.002f) {
            QSignalBlocker blk(m_panDial);
            m_panDial->setValue(pan);
        }
    }

    // Update button checked states (drives :checked QSS styling)
    if (m_muted != state.isMuted) {
        m_muted = state.isMuted;
        QSignalBlocker blk(m_muteBtn);
        m_muteBtn->setChecked(m_muted);
    }
    if (m_soloed != state.isSoloed) {
        m_soloed = state.isSoloed;
        QSignalBlocker blk(m_soloBtn);
        m_soloBtn->setChecked(m_soloed);
    }
}

void ChannelStripWidget::updateFromState(const bridge::TrackUIState& state)
{
    m_channelStripNode = state.channelStripNode;
    m_pannerNode = state.pannerNode;
    m_isFolder = (state.type == composition::TrackType::FOLDER);

    bool needsRepaint = false;
    
    if (m_isSelected != state.isSelected) {
        m_isSelected = state.isSelected;
        needsRepaint = true;
    }

    if (m_colorARGB != state.colorARGB) {
        m_colorARGB = state.colorARGB;
        if (!m_isMaster && m_typeIndicator) {
            QColor c = QColor::fromRgba(m_colorARGB);
            m_typeIndicator->setStyleSheet(
                QString("background-color: %1; border-radius: 5px;").arg(c.name(QColor::HexArgb)));
        }
        needsRepaint = true;
    }
    
    if (needsRepaint) {
        update(); // Request repaint for the top bar and background
    }

    // Call updateDynamicState to update fader, pan, mute, solo
    bridge::TrackDynamicState dynamicState;
    dynamicState.faderLeveldB = state.faderLeveldB;
    dynamicState.panPosition = state.panPosition;
    dynamicState.isMuted = state.isMuted;
    dynamicState.isSoloed = state.isSoloed;
    updateDynamicState(dynamicState);

    if (m_recordBtn && m_recordArmed != state.isRecordArmed) {
        m_recordArmed = state.isRecordArmed;
        QSignalBlocker blk(m_recordBtn);
        m_recordBtn->setChecked(m_recordArmed);
    }
    if (m_monitorBtn && m_inputMonitoring != state.isInputMonitoring) {
        m_inputMonitoring = state.isInputMonitoring;
        QSignalBlocker blk(m_monitorBtn);
        m_monitorBtn->setChecked(m_inputMonitoring);
    }

    // Update comments tooltip
    if (m_infoBtn) {
        if (std::strlen(state.comments) > 0) {
            m_infoBtn->setToolTip(QString::fromUtf8(state.comments));
            m_infoBtn->setStyleSheet("QPushButton { background: transparent; color: #525FE1; border: none; }");
        } else {
            m_infoBtn->setToolTip("Add Comments");
            m_infoBtn->setStyleSheet("QPushButton { background: transparent; color: #9DB2BF; border: none; }");
        }
    }

    // Update Input Routing Combo
    if (m_inputRoutingCombo && !m_inputRoutingCombo->view()->isVisible()) {
        QSignalBlocker blocker(m_inputRoutingCombo);
        if (state.trackInput.hasInputSlot) {
            int foundIdx = -1;
            uint32_t activeInput = state.trackInput.mappedPhysicalInputIndex;
            for (int i = 0; i < m_inputRoutingCombo->count(); ++i) {
                uint64_t data = m_inputRoutingCombo->itemData(i).toULongLong();
                uint32_t cIdx = static_cast<uint32_t>(data & 0xFFFFFFFF);
                if (cIdx == activeInput) {
                    foundIdx = i;
                    break;
                }
            }
            if (foundIdx != -1) {
                m_inputRoutingCombo->setCurrentIndex(foundIdx);
            } else {
                m_inputRoutingCombo->clear();
                uint64_t fallbackData = (static_cast<uint64_t>(state.trackInput.numChannels) << 32) | static_cast<uint64_t>(activeInput);
                m_inputRoutingCombo->addItem(QString::fromUtf8(state.trackInput.inputName), static_cast<qulonglong>(fallbackData));
                m_inputRoutingCombo->setCurrentIndex(0);
            }
        } else {
            m_inputRoutingCombo->clear();
            m_inputRoutingCombo->addItem("No Input", static_cast<qulonglong>(0xFFFFFFFF));
            m_inputRoutingCombo->setCurrentIndex(0);
        }
    }


    // Update Output Routing Combo
    if (m_outputRoutingCombo && !m_outputRoutingCombo->view()->isVisible()) {
        QSignalBlocker blocker(m_outputRoutingCombo);
        int foundIdx = -1;
        uint64_t targetRaw = state.outputTargetTrackId.toRaw();
        for (int i = 0; i < m_outputRoutingCombo->count(); ++i) {
            if (m_outputRoutingCombo->itemData(i).toULongLong() == targetRaw) {
                foundIdx = i;
                break;
            }
        }
        if (foundIdx != -1) {
            m_outputRoutingCombo->setCurrentIndex(foundIdx);
        } else {
            m_outputRoutingCombo->clear();
            if (state.outputTargetTrackId.isValid()) {
                QString targetName = "Track";
                if (m_controller) {
                    auto targetState = m_controller->getTrackState(state.outputTargetTrackId);
                    targetName = QString::fromUtf8(targetState.name);
                }
                m_outputRoutingCombo->addItem(targetName, static_cast<qulonglong>(targetRaw));
            } else {
                m_outputRoutingCombo->addItem("Master Bus", static_cast<qulonglong>(TrackID::invalid().toRaw()));
            }
            m_outputRoutingCombo->setCurrentIndex(0);
        }
    }

    // Update plugins and sends
    if (m_effectSlots)     m_effectSlots->updateFromState(state);
    if (m_preSends)        m_preSends->updateFromState(state);
    if (m_postSends)       m_postSends->updateFromState(state);
}

// ---------------------------------------------------------------------------
// Paint — dark anodized background with top color bar
// ---------------------------------------------------------------------------

void ChannelStripWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    double w = static_cast<double>(width());
    double h = static_cast<double>(height());

    QColor panelColor = m_isSelected ? theme::Color::BgControl : theme::Color::BgSurface;
    if (m_isFolder) {
        QColor folderColor = QColor::fromRgba(m_colorARGB);
        folderColor.setAlphaF(0.15f);
        panelColor = folderColor;
    }

    // Background
    theme::PaintHelper::drawGlassPanel(
        &painter, QRectF(0.0, 0.0, w, h), panelColor, 4.0);

    // Thin colored top edge bar
    QColor barColor;
    if (m_isMaster) {
        barColor = theme::Color::AccentRecord;
    } else {
        barColor = QColor::fromRgba(m_colorARGB);
    }
    painter.fillRect(QRectF(1.0, 1.0, w - 2.0, 3.0), barColor);
}

// ---------------------------------------------------------------------------
// Slots — all actions route through bridge::ITrackController
// ---------------------------------------------------------------------------

void ChannelStripWidget::onFaderChanged(float value)
{
    if (!m_controller) return;
    
    float targetGainLinear = Math::Gain::normalizedToLinear(value);
    float targetDb = Math::Gain::coeffTodB(targetGainLinear);
    float deltaDb = targetDb - m_dragStartFaderDb;

    for (const auto& [rawId, startDb] : m_dragStartTrackLevelsDb) {
        float newDb = startDb + deltaDb;
        // Clamp to sensible fader bounds (e.g. +12dB to -60dB, where -60dB typically maps to 0 linear)
        newDb = std::clamp(newDb, -180.0f, 12.0f);
        float newLinear = Math::Gain::dBToCoeff(newDb);
        m_controller->setFaderGain(TrackID::fromRaw(rawId), newLinear);
    }

    if (m_automation) {
        m_automation->recordValue(value);
    }
}

void ChannelStripWidget::onDialChanged(float value)
{
    if (!m_controller) return;

    float clampedPan = std::clamp(value, 0.0f, 1.0f);
    float deltaPan = clampedPan - m_dragStartPan;

    for (const auto& [rawId, startPan] : m_dragStartPanPositions) {
        float newPan = std::clamp(startPan + deltaPan, 0.0f, 1.0f);
        m_controller->setPan(TrackID::fromRaw(rawId), newPan);
    }

    if (m_automation) {
        m_automation->recordValue(clampedPan);
    }
}

void ChannelStripWidget::onMuteClicked()
{
    m_muted = m_muteBtn->isChecked();

    if (m_controller) {
        m_controller->setMute(m_trackId, m_muted);
    }
}

void ChannelStripWidget::onSoloClicked()
{
    m_soloed = m_soloBtn->isChecked();

    if (m_controller) {
        m_controller->setSolo(m_trackId, m_soloed);
    }
}

void ChannelStripWidget::onRecordArmClicked()
{
    if (!m_controller) return;
    m_recordArmed = m_recordBtn->isChecked();
    m_controller->setRecordArmed(m_trackId, m_recordArmed);
}

void ChannelStripWidget::onInputMonitorClicked()
{
    if (!m_controller) return;
    m_inputMonitoring = m_monitorBtn->isChecked();
    m_controller->setInputMonitoring(m_trackId, m_inputMonitoring);
}

void ChannelStripWidget::onFaderPressed()
{
    if (!m_controller) return;
    
    std::vector<bridge::TrackUIState> tracks = m_controller->getAllTracks();
    bool isSelected = false;
    for (const auto& t : tracks) {
        if (t.isSelected && t.trackId == m_trackId) isSelected = true;
    }
    
    m_dragStartTrackLevelsDb.clear();
    m_dragStartFaderDb = m_controller->getTrackState(m_trackId).faderLeveldB;

    if (isSelected) {
        for (const auto& t : tracks) {
            if (t.isSelected) m_dragStartTrackLevelsDb[t.trackId.toRaw()] = t.faderLeveldB;
        }
    } else {
        m_dragStartTrackLevelsDb[m_trackId.toRaw()] = m_dragStartFaderDb;
    }

    if (!m_automation || !m_channelStripNode.isValid()) return;
    m_automation->selectActiveAutomationLane(m_trackId, m_channelStripNode, 0, 0); // index 0 = volume
    m_automation->startTouchRecording();
}

void ChannelStripWidget::onFaderReleased()
{
    if (!m_automation) return;
    m_automation->stopTouchRecording();
}

void ChannelStripWidget::onDialPressed()
{
    if (!m_controller) return;

    std::vector<bridge::TrackUIState> tracks = m_controller->getAllTracks();
    bool isSelected = false;
    for (const auto& t : tracks) {
        if (t.isSelected && t.trackId == m_trackId) isSelected = true;
    }

    m_dragStartPanPositions.clear();
    m_dragStartPan = m_controller->getTrackState(m_trackId).panPosition;

    if (isSelected) {
        for (const auto& t : tracks) {
            if (t.isSelected) m_dragStartPanPositions[t.trackId.toRaw()] = t.panPosition;
        }
    } else {
        m_dragStartPanPositions[m_trackId.toRaw()] = m_dragStartPan;
    }

    if (!m_automation || !m_channelStripNode.isValid()) return;
    m_automation->selectActiveAutomationLane(m_trackId, m_channelStripNode, 0, 1); // index 1 = pan
    m_automation->startTouchRecording();
}

void ChannelStripWidget::onDialReleased()
{
    if (!m_automation) return;
    m_automation->stopTouchRecording();
}

void ChannelStripWidget::onCollapseClicked()
{
    m_isCollapsed = m_collapseBtn->isChecked();
    m_collapseBtn->setText(m_isCollapsed ? QString::fromUtf8("◀") : QString::fromUtf8("▼"));
    emit collapseStateToggled(m_trackId, m_isCollapsed);
}

void ChannelStripWidget::setCollapsedState(bool collapsed)
{
    if (m_isCollapsed != collapsed) {
        m_isCollapsed = collapsed;
        if (m_collapseBtn) {
            QSignalBlocker blocker(m_collapseBtn);
            m_collapseBtn->setChecked(collapsed);
            m_collapseBtn->setText(collapsed ? QString::fromUtf8("◀") : QString::fromUtf8("▼"));
        }
    }
}

bool ChannelStripWidget::eventFilter(QObject* watched, QEvent* event) {
    if (auto* editor = qobject_cast<QLineEdit*>(watched)) {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                editor->disconnect();
                editor->hide();
                editor->deleteLater();
                return true;
            }
        }
    }

    if (watched == m_nameLabel && event->type() == QEvent::MouseButtonDblClick) {
        if (m_isMaster || !m_controller) return false;

        QLineEdit* editor = new QLineEdit(m_nameLabel->text(), this);
        QRect geom = m_nameLabel->geometry();
        geom.adjust(-2, -3, 2, 3);
        editor->setGeometry(geom);
        editor->setFont(theme::Font::primary(9));
        editor->setFrame(false);
        editor->setStyleSheet(QString(
            "QLineEdit {"
            "  color: %1;"
            "  background-color: %2;"
            "  border: 1.5px solid %3;"
            "  border-radius: 4px;"
            "  padding: 1px 4px;"
            "}"
        ).arg(theme::Color::TextPrimary.name())
         .arg(theme::Color::BgControl.name())
         .arg(theme::Color::AccentGlow.name()));
        editor->selectAll();
        editor->setFocus();
        editor->show();
        editor->raise();
        editor->installEventFilter(this);

        connect(editor, &QLineEdit::editingFinished, this, [this, editor]() {
            QString newName = editor->text().trimmed();
            if (!newName.isEmpty() && m_controller) {
                m_controller->renameTrack(m_trackId, newName.toUtf8().constData());
                m_nameLabel->setText(newName.toUpper().left(8));
            }
            editor->hide();
            editor->deleteLater();
        });
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void ChannelStripWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (m_isMaster || !m_controller) {
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    if (event->position().y() <= 12.0) {
        QColor color = QColorDialog::getColor(
            QColor(
                static_cast<int>((m_colorARGB >> 16) & 0xFF),
                static_cast<int>((m_colorARGB >> 8) & 0xFF),
                static_cast<int>(m_colorARGB & 0xFF)
            ),
            this,
            QStringLiteral("Select Track Color")
        );
        if (color.isValid()) {
            uint32_t argb = static_cast<uint32_t>((color.alpha() << 24) | (color.red() << 16) | (color.green() << 8) | color.blue());
            m_controller->setTrackColor(m_trackId, argb);
        }
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ChannelStripWidget::mousePressEvent(QMouseEvent* event) {
    setFocus();
    if (event->button() == Qt::LeftButton) {
        m_dragStartPos = event->position().toPoint();
        
        if (!m_isMaster && m_controller) {
            bool multi = (event->modifiers() & Qt::ControlModifier) || (event->modifiers() & Qt::MetaModifier);
            bool range = (event->modifiers() & Qt::ShiftModifier);
            emit selectionRequested(m_trackId, multi, range);
        }
    }
    QWidget::mousePressEvent(event);
}

void ChannelStripWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if ((event->position().toPoint() - m_dragStartPos).manhattanLength() < QApplication::startDragDistance()) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (m_isMaster || !m_controller) {
        return;
    }

    QDrag* drag = new QDrag(this);
    QMimeData* mimeData = new QMimeData;

    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << m_trackId.toRaw();
    mimeData->setData("application/x-daw-mixer-track-id", data);
    drag->setMimeData(mimeData);

    QPixmap pixmap = this->grab();
    drag->setPixmap(pixmap);
    drag->setHotSpot(event->position().toPoint());

    drag->exec(Qt::MoveAction);
}

void ChannelStripWidget::showContextMenu(const QPoint& pos) {
    if (m_isMaster || !m_controller) return;

    QMenu menu(this);
    menu.setStyleSheet(theme::Style::getGlobalStyleSheet());
    QAction* actClone = menu.addAction(QStringLiteral("Duplicate Track"));
    QAction* actRemove = menu.addAction(QStringLiteral("Delete Track"));
    menu.addSeparator();

    QAction* actColor = menu.addAction(QStringLiteral("Set Track Color..."));
    menu.addSeparator();

    QAction* selectedAction = menu.exec(mapToGlobal(pos));
    if (!selectedAction) return;

    if (selectedAction == actClone) {
        m_controller->cloneTrack(m_trackId);
    } else if (selectedAction == actRemove) {
        m_controller->removeTrack(m_trackId);
    } else if (selectedAction == actColor) {
        QColor color = QColorDialog::getColor(
            QColor(
                static_cast<int>((m_colorARGB >> 16) & 0xFF),
                static_cast<int>((m_colorARGB >> 8) & 0xFF),
                static_cast<int>(m_colorARGB & 0xFF)
            ),
            this,
            QStringLiteral("Select Track Color")
        );
        if (color.isValid()) {
            uint32_t argb = static_cast<uint32_t>((color.alpha() << 24) | (color.red() << 16) | (color.green() << 8) | color.blue());
            m_controller->setTrackColor(m_trackId, argb);
        }
    }
}

void ChannelStripWidget::onInputRoutingChanged(int index) {
    if (!m_controller || !m_inputRoutingCombo) return;
    uint64_t data = m_inputRoutingCombo->itemData(index).toULongLong();
    uint32_t channelIndex = static_cast<uint32_t>(data & 0xFFFFFFFF);
    uint32_t numCh = static_cast<uint32_t>(data >> 32);
    m_controller->setTrackInput(m_trackId, channelIndex, numCh);
}

void ChannelStripWidget::onOutputRoutingChanged(int index) {
    if (!m_controller || !m_outputRoutingCombo) return;
    uint64_t rawTrackId = m_outputRoutingCombo->itemData(index).toULongLong();
    TrackID targetTrackId = TrackID::fromRaw(rawTrackId);
    m_controller->setTrackOutputRouting(m_trackId, targetTrackId);
}

void ChannelStripWidget::onInfoClicked() {
    if (!m_controller) return;
    auto state = m_controller->getTrackState(m_trackId);
    bool ok = false;
    QString commentText = DAWInputDialog::getMultiLineText(
        this, tr("Track Comments"), tr("Edit comments for this track:"),
        QString::fromUtf8(state.comments), &ok
    );
    if (ok) {
        m_controller->setTrackComments(m_trackId, commentText.toUtf8().constData());
    }
}

void ChannelStripWidget::triggerMuteToggle() {
    onMuteClicked();
}

void ChannelStripWidget::triggerSoloToggle() {
    onSoloClicked();
}

void ChannelStripWidget::triggerRecordArmToggle() {
    onRecordArmClicked();
}

void ChannelStripWidget::triggerInlineRename() {
    if (m_isMaster || !m_controller) return;
    bool ok = false;
    QString currentName = m_nameLabel ? m_nameLabel->text() : QStringLiteral("Track");
    QString text = DAWInputDialog::getText(this, QStringLiteral("Rename Track"),
                                         QStringLiteral("Track Name:"),
                                         currentName, &ok);
    if (ok && !text.isEmpty()) {
        m_controller->renameTrack(m_trackId, text.toUtf8().constData());
    }
}

void ChannelStripWidget::keyPressEvent(QKeyEvent* event) {
    int key = event->key();
    Qt::KeyboardModifiers mods = event->modifiers();

    if (key == Qt::Key_M && !(mods & Qt::AltModifier)) {
        triggerMuteToggle();
        return;
    }
    if (key == Qt::Key_S && !(mods & Qt::AltModifier)) {
        triggerSoloToggle();
        return;
    }
    if (key == Qt::Key_C || (key == Qt::Key_R && (mods & Qt::ShiftModifier))) {
        triggerRecordArmToggle();
        return;
    }
    if (key == Qt::Key_F2) {
        triggerInlineRename();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace presentation::views
