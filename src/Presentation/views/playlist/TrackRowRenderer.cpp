// src/Presentation/views/playlist/TrackRowRenderer.cpp
#include "TrackRowRenderer.h"
#include "../theme.h"
#include <QPainterPath>
#include <QLinearGradient>
#include "Middle Bridge/automation/automation_helpers.h"

namespace presentation::views {

void TrackRowRenderer::calculateControlRects(const QRectF& rowRect,
                                             const bridge::TrackUIState& state,
                                             int depth,
                                             QRectF& outMute, QRectF& outSolo,
                                             QRectF& outArm, QRectF& outMonitor,
                                             QRectF& outCombo,
                                             QRectF& outAutoExpand, QRectF& outTakesExpand,
                                             QRectF& outName)
{
    const double w = rowRect.width();
    const double h = rowRect.height();
    const double rx = rowRect.x();
    const double ry = rowRect.y();

    // Mute/Solo/Record/Monitor Buttons
    double btnY = 32.0;
    if ((state.hasInstrumentSlot || state.hasAudioInputSlot || state.type == composition::TrackType::AUDIO) && h >= 90.0) {
        btnY = 60.0;
    }
    outMute = QRectF(rx + 12.0, ry + btnY, 24.0, 24.0);
    outSolo = QRectF(rx + 44.0, ry + btnY, 24.0, 24.0);
    outArm = QRectF(rx + 76.0, ry + btnY, 24.0, 24.0);
    outMonitor = QRectF(rx + 108.0, ry + btnY, 24.0, 24.0);

    // Name label rect
    const double nameIndent = std::min(depth * 12.0, 48.0);
    const double nameX = 12.0 + nameIndent;
    if (h < 40.0) {
        outName = QRectF(rx + nameX, ry, w - 70.0 - nameIndent, h);
    } else {
        outName = QRectF(rx + nameX, ry + 8.0, w - 70.0 - nameIndent, 16.0);
    }

    // Automation control visibility check (showAutomation)
    bool showAutomation = false;
    if (state.hasInstrumentSlot || state.hasAudioInputSlot || state.type == composition::TrackType::AUDIO) {
        if (h >= 120.0) showAutomation = true;
    } else {
        if (h >= static_cast<double>(bridge::kMainLaneHeightDefault)) showAutomation = true;
    }

    if (showAutomation) {
        bool showTakes = (state.audioLanesCount > 1);
        if (state.hasInstrumentSlot || state.hasAudioInputSlot || state.type == composition::TrackType::AUDIO) {
            outCombo = QRectF(rx + 12.0, ry + 92.0, 95.0, 22.0);
            outAutoExpand = QRectF(rx + 111.0, ry + 92.0, 65.0, 22.0);
            if (showTakes) {
                outTakesExpand = QRectF(rx + 180.0, ry + 92.0, 65.0, 22.0);
            } else {
                outTakesExpand = QRectF();
            }
        } else {
            outCombo = QRectF(rx + 12.0, ry + 64.0, 95.0, 22.0);
            outAutoExpand = QRectF(rx + 111.0, ry + 64.0, 65.0, 22.0);
            if (showTakes) {
                outTakesExpand = QRectF(rx + 180.0, ry + 64.0, 65.0, 22.0);
            } else {
                outTakesExpand = QRectF();
            }
        }
    } else {
        outCombo = QRectF();
        outAutoExpand = QRectF();
        outTakesExpand = QRectF();
    }
}

void TrackRowRenderer::paint(QPainter& p,
                             const QRectF& rect,
                             const bridge::TrackUIState& state,
                             const QRectF& muteRect,
                             const QRectF& soloRect,
                             const QRectF& armRect,
                             const QRectF& monitorRect,
                             const QRectF& comboRect,
                             const QRectF& autoExpandRect,
                             const QRectF& takesExpandRect,
                             const QRectF& nameRect,
                             bool isSelected,
                             bool isGrouped,
                             int depth,
                             const QString& iconPreset,
                             float peakLeftNorm,
                             float peakRightNorm,
                             VirtualControl hoveredControl,
                             VirtualControl pressedControl)
{
    const double w = rect.width();
    const double h = rect.height();
    const double rx = rect.x();
    const double ry = rect.y();
    (void)nameRect;

    // 1. Draw base background
    bool isFolder = (state.type == composition::TrackType::FOLDER);
    QColor panelColor = isSelected ? theme::Color::BgControl : theme::Color::BgSurface;
    
    if (isFolder) {
        QColor folderColor = QColor::fromRgba(state.colorARGB);
        folderColor.setAlphaF(0.15f);
        panelColor = folderColor;
    } else if (isGrouped && !isSelected) {
        // Reduce opacity of grouped children to let the folder tray shine through
        panelColor.setAlphaF(0.5f);
    }

    theme::PaintHelper::drawGlassPanel(
        &p, rect.toRect(), panelColor, 0.0);

    // 2. Draw border lines
    p.setPen(QPen(theme::Color::BgControl, 1.0));
    p.drawLine(QPointF(rx, ry + h - 1.0), QPointF(rx + w, ry + h - 1.0));
    p.drawLine(QPointF(rx + w - 1.0, ry), QPointF(rx + w - 1.0, ry + h));

    // 3. Draw Track Color Strip
    QColor trackColor = QColor::fromRgba(state.colorARGB);
    p.fillRect(QRectF(rx, ry, 4.0, h), trackColor);

    // Visual Grouping Bracket
    if (isGrouped) {
        QColor groupedPenColor = theme::Color::AccentGlow;
        groupedPenColor.setAlpha(220);
        p.setPen(QPen(groupedPenColor, 1.5));
        p.drawLine(QPointF(rx + 6.0, ry), QPointF(rx + 6.0, ry + h));
        p.drawLine(QPointF(rx + 6.0, ry), QPointF(rx + 10.0, ry));
    }

    // 4. Draw Track Name and Icon
    p.setPen(theme::Color::TextPrimary);
    p.setFont(theme::Font::primary(9, QFont::Bold));

    QString nameStr = QString::fromUtf8(state.name);
    QString iconPrefix;
    if (iconPreset == "Audio") iconPrefix = QString::fromUtf8("🔊 ");
    else if (iconPreset == "MIDI") iconPrefix = QString::fromUtf8("🎹 ");
    else if (iconPreset == "Synth") iconPrefix = QString::fromUtf8("🎛️ ");
    else if (iconPreset == "Guitar") iconPrefix = QString::fromUtf8("🎸 ");
    else if (iconPreset == "Drums") iconPrefix = QString::fromUtf8("🥁 ");
    else if (iconPreset == "Vocals") iconPrefix = QString::fromUtf8("🎤 ");

    QString displayName = iconPrefix + nameStr;

    const double nameIndent = std::min(depth * 12.0, 48.0);
    const double nameX = 12.0 + nameIndent;

    if (h < 40.0) {
        p.drawText(QRectF(rx + nameX, ry, w - 70.0 - nameIndent, h), Qt::AlignVCenter | Qt::AlignLeft, displayName);
    } else {
        p.drawText(QRectF(rx + nameX, ry + 8.0, w - 70.0 - nameIndent, 16.0), Qt::AlignVCenter | Qt::AlignLeft, displayName);

        // 5. Draw Mute, Solo, Record Arm, and Input Monitor buttons
        p.setPen(Qt::NoPen);

        // Mute Button
        bool mutePressed = (pressedControl == VirtualControl::MuteButton && hoveredControl == VirtualControl::MuteButton);
        bool muteHovered = (hoveredControl == VirtualControl::MuteButton);
        p.setBrush(state.isMuted ? theme::Color::BtnMuteActive : (mutePressed ? theme::Color::BgControl.darker(110) : (muteHovered ? theme::Color::BgControl.lighter(110) : theme::Color::BgControl)));
        p.drawRoundedRect(muteRect, 4.0, 4.0);
        p.setPen(state.isMuted ? theme::Color::BtnMuteText : theme::Color::TextMuted);
        p.setFont(theme::Font::primary(9, QFont::Bold));
        p.drawText(muteRect, Qt::AlignCenter, "M");

        // Solo Button
        p.setPen(Qt::NoPen);
        bool soloPressed = (pressedControl == VirtualControl::SoloButton && hoveredControl == VirtualControl::SoloButton);
        bool soloHovered = (hoveredControl == VirtualControl::SoloButton);
        p.setBrush(state.isSoloed ? theme::Color::BtnSoloActive : (soloPressed ? theme::Color::BgControl.darker(110) : (soloHovered ? theme::Color::BgControl.lighter(110) : theme::Color::BgControl)));
        p.drawRoundedRect(soloRect, 4.0, 4.0);
        p.setPen(state.isSoloed ? theme::Color::BtnSoloText : theme::Color::TextMuted);
        p.drawText(soloRect, Qt::AlignCenter, "S");

    }

    // 6. Arm Button
    if (armRect.isValid()) {
        bool aPressed = (pressedControl == VirtualControl::ArmButton && hoveredControl == VirtualControl::ArmButton);
        bool aHovered = (hoveredControl == VirtualControl::ArmButton);
        p.setPen(Qt::NoPen);
        if (state.isRecordArmed) {
            p.setBrush(theme::Color::AccentRecord);
        } else if (aPressed) {
            p.setBrush(theme::Color::BgControl.lighter(120));
        } else if (aHovered) {
            p.setBrush(theme::Color::BgControl.lighter(110));
        } else {
            p.setBrush(theme::Color::BgControl);
        }
        p.drawRoundedRect(armRect, 3.0, 3.0);

        p.setPen(state.isRecordArmed ? theme::Color::TextPrimary : theme::Color::TextMuted);
        p.setFont(theme::Font::monospace(8, QFont::Bold));
        p.drawText(armRect, Qt::AlignCenter, "R");
    }

    // 7. Monitor Button
    if (monitorRect.isValid()) {
        bool monPressed = (pressedControl == VirtualControl::MonitorButton && hoveredControl == VirtualControl::MonitorButton);
        bool monHovered = (hoveredControl == VirtualControl::MonitorButton);
        p.setPen(Qt::NoPen);
        if (state.isInputMonitoring) {
            p.setBrush(theme::Color::SendPreFader);
        } else if (monPressed) {
            p.setBrush(theme::Color::BgControl.lighter(120));
        } else if (monHovered) {
            p.setBrush(theme::Color::BgControl.lighter(110));
        } else {
            p.setBrush(theme::Color::BgControl);
        }
        p.drawRoundedRect(monitorRect, 3.0, 3.0);

        p.setPen(state.isInputMonitoring ? theme::Color::BgBase : theme::Color::TextMuted);
        p.setFont(theme::Font::monospace(8, QFont::Bold));
        p.drawText(monitorRect, Qt::AlignCenter, "I");
    }

    // 6. Draw Combo and Automation Buttons
    if (comboRect.isValid() && autoExpandRect.isValid()) {
        p.setPen(QPen(theme::Color::BgControl, 1.0));
        p.setBrush(theme::Color::BgSurface);
        p.drawRoundedRect(comboRect, 4.0, 4.0);

        p.setPen(theme::Color::TextPrimary);
        p.setFont(theme::Font::monospace(7, QFont::Bold));
        
        QString modeStrs[] = {"Off", "Read", "Touch", "Latch", "Write", "Trim"};
        int idx = static_cast<int>(state.automationMode);
        QString modeText = (idx >= 0 && idx < 6) ? modeStrs[idx] : "Off";
        
        QRectF comboTextRect = comboRect.adjusted(8, 0, -20, 0);
        p.drawText(comboTextRect, Qt::AlignVCenter | Qt::AlignLeft, modeText);
        
        p.setFont(theme::Font::primary(6));
        p.drawText(comboRect.adjusted(0, 0, -8, 0), Qt::AlignVCenter | Qt::AlignRight, "▼");

        bool autoPressed = (pressedControl == VirtualControl::AutomationExpand && hoveredControl == VirtualControl::AutomationExpand);
        bool autoHovered = (hoveredControl == VirtualControl::AutomationExpand);
        p.setPen(QPen(theme::Color::BgControl, 1.0));
        p.setBrush(autoPressed ? theme::Color::BgControl : (autoHovered ? theme::Color::BgSurface.lighter(105) : theme::Color::BgSurface));
        p.drawRoundedRect(autoExpandRect, 4.0, 4.0);

        p.setPen(theme::Color::AccentGlow);
        p.setFont(theme::Font::monospace(7, QFont::Bold));
        p.drawText(autoExpandRect, Qt::AlignCenter, state.isAutomationExpanded ? QString::fromUtf8("▲ Auto") : QString::fromUtf8("▼ Auto"));

        if (takesExpandRect.isValid() && state.audioLanesCount > 1) {
            bool takesPressed = (pressedControl == VirtualControl::TakesExpand && hoveredControl == VirtualControl::TakesExpand);
            bool takesHovered = (hoveredControl == VirtualControl::TakesExpand);
            p.setPen(QPen(theme::Color::BgControl, 1.0));
            p.setBrush(takesPressed ? theme::Color::BgControl : (takesHovered ? theme::Color::BgSurface.lighter(105) : theme::Color::BgSurface));
            p.drawRoundedRect(takesExpandRect, 4.0, 4.0);

            p.setPen(theme::Color::AccentGlow);
            p.setFont(theme::Font::monospace(7, QFont::Bold));
            p.drawText(takesExpandRect, Qt::AlignCenter, state.isTakesExpanded ? QString::fromUtf8("▲ Takes") : QString::fromUtf8("▼ Takes"));
        }
    }

    // 8. Draw Stereo Peak Level Meter
    if (h >= 30.0) {
        const double meterH = h - 16.0;
        const double meterY = ry + 8.0;
        const double meterW = 6.0;
        const double leftX = rx + w - 32.0;
        const double rightX = rx + w - 24.0;

        p.fillRect(QRectF(leftX, meterY, meterW, meterH), theme::Color::BgBase);
        p.fillRect(QRectF(rightX, meterY, meterW, meterH), theme::Color::BgBase);

        const double leftFillH = static_cast<double>(peakLeftNorm) * meterH;
        const double rightFillH = static_cast<double>(peakRightNorm) * meterH;

        QRectF leftFillRect(leftX, meterY + meterH - leftFillH, meterW, leftFillH);
        QRectF rightFillRect(rightX, meterY + meterH - rightFillH, meterW, rightFillH);

        QLinearGradient grad(leftX, meterY + meterH, leftX, meterY);
        grad.setColorAt(0.0, theme::Color::BtnSoloActive);
        grad.setColorAt(0.7, theme::Color::BtnMuteActive);
        grad.setColorAt(1.0, theme::Color::AccentRecord);

        p.fillRect(leftFillRect, grad);
        p.fillRect(rightFillRect, grad);
    }
}

VirtualControl TrackRowRenderer::hitTest(const QPointF& localPos,
                                         const QRectF& rect,
                                         const QRectF& muteRect,
                                         const QRectF& soloRect,
                                         const QRectF& armRect,
                                         const QRectF& monitorRect,
                                         const QRectF& comboRect,
                                         const QRectF& autoExpandRect,
                                         const QRectF& takesExpandRect,
                                         const QRectF& nameRect)
{
    if (muteRect.contains(localPos)) return VirtualControl::MuteButton;
    if (soloRect.contains(localPos)) return VirtualControl::SoloButton;
    if (armRect.contains(localPos)) return VirtualControl::ArmButton;
    if (monitorRect.contains(localPos)) return VirtualControl::MonitorButton;
    if (comboRect.contains(localPos)) return VirtualControl::AutomationCombo;
    if (autoExpandRect.contains(localPos)) return VirtualControl::AutomationExpand;
    if (takesExpandRect.isValid() && takesExpandRect.contains(localPos)) return VirtualControl::TakesExpand;
    if (nameRect.contains(localPos)) return VirtualControl::NameLabel;

    // Check bottom border resize hit zone (4px height)
    QRectF bottomBorder(rect.x(), rect.bottom() - 4.0, rect.width(), 4.0);
    if (bottomBorder.contains(localPos)) return VirtualControl::BottomBorder;

    return VirtualControl::None;
}

} // namespace presentation::views
