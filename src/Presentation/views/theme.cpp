// src/Presentation/views/theme.cpp
#include "theme.h"
#include <QRadialGradient>
#include <QLinearGradient>
#include <QPen>
#include <QFontDatabase>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QWidget>
#include <QStyle>
#include <QSvgRenderer>
#include <QPixmap>
#include <iostream>

namespace presentation::theme {

// --- Color Constant Definitions ---
const QColor Color::BgBase       = QColor(22, 22, 22);       // #161616 (Darkest background)
const QColor Color::BgSurface    = QColor(34, 34, 34);       // #222222 (Dark warm gray panel background)
const QColor Color::BgControl    = QColor(50, 50, 50);       // #323232 (Medium warm gray controls background)
const QColor Color::AccentGlow   = QColor(167, 139, 250);    // #A78BFA (Soft violet indicator)
const QColor Color::AccentRecord = QColor(255, 59, 48);      // #FF3B30 (Arming red indicator)
const QColor Color::AccentMIDI   = QColor(168, 85, 247);     // #A855F7 (MIDI tracks amethyst)
const QColor Color::AccentAudio  = QColor(59, 130, 246);     // #3B82F6 (Audio tracks cobalt)
const QColor Color::TextPrimary  = QColor(232, 232, 232);    // #E8E8E8 (Warm white active text)
const QColor Color::TextMuted    = QColor(136, 136, 136);    // #888888 (Muted warm gray text)

const QColor Color::BgAlertMuted     = QColor(26, 13, 13);       // #1A0D0D (Muted red alert bg)
const QColor Color::BorderAlertMuted = QColor(51, 26, 26);       // #331A1A (Border alert muted)
const QColor Color::BorderAlert      = QColor(255, 85, 85);      // #FF5555 (Accent red alert border)
const QColor Color::GridOverlay      = QColor(255, 255, 255, 6); // Subtle grid overlay

const QColor Color::BtnMuteActive    = QColor(212, 168, 75);   // #D4A84B (Amber)
const QColor Color::BtnMuteText      = QColor(22, 22, 22);     // #161616 (Dark)
const QColor Color::BtnSoloActive    = QColor(45, 168, 126);   // #2DA87E (Jade Green)
const QColor Color::BtnSoloText      = QColor(22, 22, 22);     // #161616 (Dark)
const QColor Color::BtnRecordText    = QColor(232, 232, 232);  // #E8E8E8 (Light)
const QColor Color::BtnMonitorActive = QColor(59, 130, 246);   // #3B82F6 (Blue)
const QColor Color::BtnMonitorText   = QColor(232, 232, 232);  // #E8E8E8 (Light)

// Slot backgrounds and borders
const QColor Color::BgSlotEmpty         = QColor(28, 30, 36);       // #1C1E24
const QColor Color::BorderSlotEmpty     = QColor(43, 48, 60);       // #2B303C
const QColor Color::BgSlotBypassed      = QColor(44, 50, 63);       // #2C323F
const QColor Color::BorderSlotBypassed  = QColor(63, 71, 88);       // #3F4758
const QColor Color::BgSlotActive        = QColor(58, 68, 84);       // #3A4454
const QColor Color::BorderSlotActive    = QColor(80, 94, 120);      // #505E78

// Plugin active and bypassed states
const QColor Color::PluginActive        = QColor(230, 92, 0);       // #E65C00 (Premium Orange)
const QColor Color::PluginBypassed      = QColor(126, 138, 159);    // #7E8A9F (Soft gray-blue)

// Auxiliary sends active states
const QColor Color::SendPreFader        = QColor(0, 210, 180);      // #00D2B4 (Teal/Cyber Mint)
const QColor Color::SendPostFader       = QColor(80, 150, 255);     // #5096FF (Bright Blue)

// Telemetry safety metrics
const QColor Color::SafetyAmber         = QColor(255, 179, 0);      // #FFB300 (Amber unity)
const QColor Color::PeakHoldWhite       = QColor(255, 255, 255, 200);
const QColor Color::ClipIndicatorOff    = QColor(40, 20, 22);       // Deep dim warning indicator

// Extended text tokens
const QColor Color::TextSecondary       = QColor(157, 178, 191);    // #9DB2BF (Muted silver/blue-gray)
const QColor Color::TextLight           = QColor(221, 230, 237);    // #DDE6ED (Soft light white)

// Fader & Knob overlays
const QColor Color::ControlShadow       = QColor(0, 0, 0, 160);
const QColor Color::ControlHighlight    = QColor(255, 255, 255, 30);

// --- Font Configuration ---
namespace {
int scaleFontSize(int pointSize) {
    if (pointSize <= 5) return 9;
    if (pointSize == 6) return 9;
    if (pointSize == 7) return 10;
    if (pointSize == 8) return 11;
    if (pointSize == 9) return 12;
    if (pointSize == 10) return 13;
    if (pointSize == 11) return 14;
    if (pointSize == 14) return 18;
    if (pointSize >= 22) return pointSize + 4;
    return pointSize + 2;
}
} // namespace

void Font::initialize() {
    QString appDir = QCoreApplication::applicationDirPath();
    
    // Candidates for the fonts directory path
    QStringList candidates = {
        appDir + "/../../src/assets/fonts",           // Development build directory
        appDir + "/assets/fonts",                     // Standard asset deployment layout
        appDir + "/../Resources/fonts",               // macOS app bundle layout
        QDir::currentPath() + "/src/assets/fonts",   // Run from workspace root
        "/Users/goldenfung/Documents/agent-based-daw/src/assets/fonts" // Explicit absolute fallback
    };

    QString foundPath;
    for (const QString& candidate : candidates) {
        QDir dir(candidate);
        if (dir.exists() && dir.exists("Inter")) {
            foundPath = dir.canonicalPath();
            break;
        }
    }

    if (foundPath.isEmpty()) {
        std::cerr << "Theme: Warning: Could not locate fonts assets directory in any standard paths!" << std::endl;
        return;
    }

    // List of font files to load
    QStringList fontFiles = {
        foundPath + "/Inter/Inter-VariableFont_opsz,wght.ttf",
        foundPath + "/Inter/Inter-Italic-VariableFont_opsz,wght.ttf"
    };

    for (const QString& fontFile : fontFiles) {
        if (!QFile::exists(fontFile)) {
            std::cerr << "Theme: Font file not found: " << fontFile.toStdString() << std::endl;
            continue;
        }
        int id = QFontDatabase::addApplicationFont(fontFile);
        if (id == -1) {
            std::cerr << "Theme: Failed to load application font: " << fontFile.toStdString() << std::endl;
        }
    }
}

QFont Font::primary(int pointSize, int weight, double letterSpacingPercent) {
    QFont font("Inter");
    font.setStyleHint(QFont::SansSerif);
    font.setPointSize(scaleFontSize(pointSize));
    font.setWeight(static_cast<QFont::Weight>(weight));
    if (letterSpacingPercent != 100.0) {
        font.setLetterSpacing(QFont::PercentageSpacing, letterSpacingPercent);
    }
    return font;
}

QFont Font::monospace(int pointSize, int weight, double letterSpacingPercent) {
    QFont font("Inter");
    // We still use Inter, but we might eventually enable OpenType tabular figures
    font.setStyleHint(QFont::SansSerif);
    font.setPointSize(scaleFontSize(pointSize));
    font.setWeight(static_cast<QFont::Weight>(weight));
    if (letterSpacingPercent != 100.0) {
        font.setLetterSpacing(QFont::PercentageSpacing, letterSpacingPercent);
    }
    return font;
}

// --- Global QSS Style Rules ---
QString Style::getGlobalStyleSheet() {
    return getCoreStyleSheet() +
           getButtonStyleSheet() +
           getMenuStyleSheet() +
           getFieldStyleSheet() +
           getListViewStyleSheet();
}

QString Style::getCoreStyleSheet() {
    return QString(
        "/* Main Application Shell */\n"
        "QMainWindow, QDialog {\n"
        "    background-color: %1;\n"
        "    color: %2;\n"
        "}\n\n"
        "QLabel {\n"
        "    color: %2;\n"
        "    background: transparent;\n"
        "    border: none;\n"
        "}\n\n"
        "QDialog QLabel {\n"
        "    padding-bottom: 4px;\n"
        "    background: transparent;\n"
        "}\n\n"
        "/* Panels and Base Containers */\n"
        "QFrame, QWidget#mixerPanel, QWidget#timelinePanel, QWidget#editorContent {\n"
        "    background-color: %3;\n"
        "    border: none;\n"
        "    border-radius: 6px;\n"
        "}\n\n"
        "/* Dock Widgets */\n"
        "QDockWidget {\n"
        "    color: %5;\n"
        "    titlebar-close-icon: none;\n"
        "    titlebar-normal-icon: none;\n"
        "}\n"
        "QDockWidget::title {\n"
        "    background-color: %1;\n"
        "    text-align: center;\n"
        "    padding: 8px;\n"
        "    font-family: 'Inter';\n"
        "    font-size: 13px;\n"
        "    font-weight: 600;\n"
        "    color: %5;\n"
        "    border: none;\n"
        "}\n\n"
        "/* Toolbar Divider Lines */\n"
        "QWidget#divider {\n"
        "    background-color: transparent;\n"
        "}\n\n"
        "/* Splitters */\n"
        "QSplitter::handle {\n"
        "    background-color: transparent;\n"
        "}\n"
        "QSplitter::handle:horizontal {\n"
        "    width: 4px;\n"
        "}\n"
        "QSplitter::handle:vertical {\n"
        "    height: 4px;\n"
        "}\n\n"
        "/* Scroll Areas */\n"
        "QScrollArea {\n"
        "    background: transparent;\n"
        "    border: none;\n"
        "}\n\n"
        "/* Custom Scrollbars to blend with dark industrial look */\n"
        "QScrollBar:vertical {\n"
        "    background: transparent;\n"
        "    width: 8px;\n"
        "    margin: 0px;\n"
        "}\n"
        "QScrollBar::handle:vertical {\n"
        "    background: %5;\n"
        "    min-height: 24px;\n"
        "    border-radius: 4px;\n"
        "}\n"
        "QScrollBar::handle:vertical:hover {\n"
        "    background: %6;\n"
        "}\n"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {\n"
        "    height: 0px;\n"
        "}\n"
        "QScrollBar:horizontal {\n"
        "    background: transparent;\n"
        "    height: 8px;\n"
        "    margin: 0px;\n"
        "}\n"
        "QScrollBar::handle:horizontal {\n"
        "    background: %5;\n"
        "    min-width: 24px;\n"
        "    border-radius: 4px;\n"
        "}\n"
        "QScrollBar::handle:horizontal:hover {\n"
        "    background: %6;\n"
        "}\n"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {\n"
        "    width: 0px;\n"
        "}\n\n"
        "/* Sleek Tooltips */\n"
        "QToolTip {\n"
        "    background-color: %4;\n"
        "    color: %2;\n"
        "    border: none;\n"
        "    border-radius: 6px;\n"
        "    font-family: 'Inter';\n"
        "    font-size: 12px;\n"
        "    padding: 6px 10px;\n"
        "}\n\n"
        "/* DAWInputDialog Custom Styling */\n"
        "QDialog#DAWInputDialog {\n"
        "    background-color: %1;\n"
        "    border: 1px solid %4;\n"
        "    border-radius: 6px;\n"
        "}\n"
        "QDialog#DAWInputDialog QLabel {\n"
        "    color: %5;\n"
        "    padding-top: 6px;\n"
        "    padding-bottom: 6px;\n"
        "}\n"
        "QDialog#DAWInputDialog QLineEdit, QDialog#DAWInputDialog QTextEdit, QDialog#DAWInputDialog QDoubleSpinBox {\n"
        "    background-color: %3;\n"
        "    color: %2;\n"
        "    border: 1px solid %4;\n"
        "    border-radius: 4px;\n"
        "    padding: 6px;\n"
        "}\n"
        "QDialog#DAWInputDialog QLineEdit:focus, QDialog#DAWInputDialog QTextEdit:focus, QDialog#DAWInputDialog QDoubleSpinBox:focus {\n"
        "    border: 1px solid %6;\n"
        "}\n"
        "QDialog#DAWInputDialog QPushButton {\n"
        "    background-color: %4;\n"
        "    color: %2;\n"
        "    border: none;\n"
        "    border-radius: 4px;\n"
        "    padding: 6px 12px;\n"
        "    font-size: 11px;\n"
        "    font-weight: bold;\n"
        "}\n"
        "QDialog#DAWInputDialog QPushButton:hover {\n"
        "    background-color: #424242;\n"
        "    color: #FFFFFF;\n"
        "}\n"
        "QDialog#DAWInputDialog QPushButton:default {\n"
        "    background-color: %6;\n"
        "    color: %1;\n"
        "}\n"
        "QDialog#DAWInputDialog QPushButton:default:hover {\n"
        "    background-color: #C4B5FD;\n"
        "    color: %1;\n"
        "}\n\n"
        "/* Mixer Specific Labels */\n"
        "QLabel#trackNameLabel {\n"
        "    color: %7;\n"
        "    background: transparent;\n"
        "    border: none;\n"
        "}\n"
        "QLabel#sendPopupHeader {\n"
        "    color: %8;\n"
        "}\n"
        "QLabel#sendPopupTitle {\n"
        "    color: %7;\n"
        "}\n"
        "QLabel#sendPopupValue {\n"
        "    color: %8;\n"
        "}\n"
        "QLabel#sendPopupValueDisabled {\n"
        "    color: #526D82;\n"
        "}\n"
    )
    .arg(Color::BgBase.name())
    .arg(Color::TextPrimary.name())
    .arg(Color::BgSurface.name())
    .arg(Color::BgControl.name())
    .arg(Color::TextMuted.name())
    .arg(Color::AccentGlow.name())
    .arg(Color::TextSecondary.name())
    .arg(Color::TextLight.name());
}

QString Style::getButtonStyleSheet() {
    QString qss = 
        "/* Buttons & ToolButtons Base Style */\n"
        "QPushButton, QToolButton {\n"
        "    background-color: @BgSurface;\n"
        "    border: none;\n"
        "    border-radius: 6px;\n"
        "    color: @TextPrimary;\n"
        "    padding: 6px 16px;\n"
        "    font-family: 'Inter';\n"
        "    font-size: 13px;\n"
        "    font-weight: 500;\n"
        "}\n"
        "QPushButton:hover, QToolButton:hover {\n"
        "    background-color: @BgControl;\n"
        "    color: #FFFFFF;\n"
        "}\n"
        "QPushButton:pressed, QToolButton:pressed {\n"
        "    background-color: @BgBase;\n"
        "    color: @TextPrimary;\n"
        "}\n"
        "QPushButton:checked, QToolButton:checked {\n"
        "    background-color: @AccentGlow;\n"
        "    color: @TextPrimary;\n"
        "}\n"
        "QPushButton:disabled, QToolButton:disabled {\n"
        "    background-color: transparent;\n"
        "    color: #565C66;\n"
        "}\n\n"
        "/* Menu indicators for buttons with dropdowns */\n"
        "QPushButton::menu-indicator {\n"
        "    width: 8px;\n"
        "    subcontrol-position: right center;\n"
        "    subcontrol-origin: padding;\n"
        "    padding-right: 4px;\n"
        "}\n\n"
        "/* Channel Strip Action Buttons (M/S/R/I) - small format for tight layout */\n"
        "QPushButton#muteBtn, QPushButton#soloBtn, QPushButton#recordBtn, QPushButton#monitorBtn {\n"
        "    border: none;\n"
        "    padding: 2px 2px;\n"
        "    border-radius: 4px;\n"
        "    font-size: 11px;\n"
        "    font-weight: 700;\n"
        "    color: @TextMuted;\n"
        "    background-color: @BgControl;\n"
        "}\n"
        "QPushButton#muteBtn:hover, QPushButton#soloBtn:hover, "
        "QPushButton#recordBtn:hover, QPushButton#monitorBtn:hover {\n"
        "    background-color: #444444;\n"
        "}\n\n"
        "/* Channel Strip Mute Button */\n"
        "QPushButton#muteBtn:checked {\n"
        "    background-color: @BtnMuteActive;\n"
        "    color: @BtnMuteText;\n"
        "}\n\n"
        "/* Channel Strip Solo Button */\n"
        "QPushButton#soloBtn:checked {\n"
        "    background-color: @BtnSoloActive;\n"
        "    color: @BtnSoloText;\n"
        "}\n\n"
        "/* Channel Strip Record Button */\n"
        "QPushButton#recordBtn:checked {\n"
        "    background-color: @AccentRecord;\n"
        "    color: @BtnRecordText;\n"
        "}\n\n"
        "/* Channel Strip Monitor Button */\n"
        "QPushButton#monitorBtn:checked {\n"
        "    background-color: @BtnMonitorActive;\n"
        "    color: @BtnMonitorText;\n"
        "}\n\n"
        "/* Unified Plugin Header Action Button Styles (Bypass, Remove, Add Automation) */\n"
        "QPushButton[class=\"header-btn\"] {\n"
        "    font-family: 'Inter';\n"
        "    font-size: 11px;\n"
        "    font-weight: 700;\n"
        "    border: none;\n"
        "    border-radius: 6px;\n"
        "    padding: 3px 12px;\n"
        "}\n"
        "QPushButton[state=\"bypass-active\"], QPushButton[variant=\"accent\"] {\n"
        "    background-color: #004D40;\n"
        "    color: #00F5D4;\n"
        "    border: none;\n"
        "    border-radius: 6px;\n"
        "    padding: 3px 12px;\n"
        "    font-family: 'Inter';\n"
        "    font-size: 11px;\n"
        "    font-weight: 700;\n"
        "}\n"
        "QPushButton[state=\"bypass-active\"]:hover, QPushButton[variant=\"accent\"]:hover {\n"
        "    background-color: #00F5D4;\n"
        "    color: #0B0C0E;\n"
        "}\n"
        "QPushButton[state=\"bypass-inactive\"], QPushButton[variant=\"warning\"] {\n"
        "    background-color: #5E3600;\n"
        "    color: #FFAA00;\n"
        "    border: none;\n"
        "    border-radius: 6px;\n"
        "    padding: 3px 12px;\n"
        "    font-family: 'Inter';\n"
        "    font-size: 11px;\n"
        "    font-weight: 700;\n"
        "}\n"
        "QPushButton[state=\"bypass-inactive\"]:hover, QPushButton[variant=\"warning\"]:hover {\n"
        "    background-color: #FFAA00;\n"
        "    color: #0B0C0E;\n"
        "}\n"
        "QPushButton[state=\"remove\"], QPushButton[variant=\"danger\"] {\n"
        "    background-color: #59161E;\n"
        "    color: #FF6B6B;\n"
        "    border: none;\n"
        "    border-radius: 6px;\n"
        "    padding: 3px 12px;\n"
        "    font-family: 'Inter';\n"
        "    font-size: 11px;\n"
        "    font-weight: 700;\n"
        "}\n"
        "QPushButton[state=\"remove\"]:hover, QPushButton[variant=\"danger\"]:hover {\n"
        "    background-color: #FF3B30;\n"
        "    color: #FFFFFF;\n"
        "}\n"
        "QPushButton[variant=\"action\"] {\n"
        "    background-color: #004D40;\n"
        "    color: #00F5D4;\n"
        "    border: none;\n"
        "    border-radius: 6px;\n"
        "    padding: 3px 12px;\n"
        "    font-family: 'Inter';\n"
        "    font-size: 11px;\n"
        "    font-weight: 700;\n"
        "}\n"
        "QPushButton[variant=\"action\"]:hover {\n"
        "    background-color: #00F5D4;\n"
        "    color: #0B0C0E;\n"
        "}\n\n"
        "/* Browser Preview Deck Play/Stop State */\n"
        "QPushButton[state=\"preview-playing\"] {\n"
        "    background-color: #FF3B30;\n"
        "    color: #FFFFFF;\n"
        "}\n"
        "QPushButton[state=\"preview-playing\"]:hover {\n"
        "    background-color: #FF453A;\n"
        "}\n"
        "QPushButton[state=\"preview-stopped\"] {\n"
        "    background-color: @AccentGlow;\n"
        "    color: @BgBase;\n"
        "}\n"
        "QPushButton[state=\"preview-stopped\"]:hover {\n"
        "    background-color: #C4B5FD;\n"
        "}\n\n"
        "/* Specialized Header Action Buttons */\n"
        "QPushButton#infoBtn, QPushButton#collapseBtn {\n"
        "    background: transparent;\n"
        "    color: @TextSecondary;\n"
        "    border: none;\n"
        "    padding: 0px;\n"
        "    margin: 0px;\n"
        "}\n"
        "QPushButton#infoBtn:hover, QPushButton#collapseBtn:hover {\n"
        "    color: @TextPrimary;\n"
        "}\n\n"
        "/* Centralized CheckBox styling */\n"
        "QCheckBox {\n"
        "    color: @TextSecondary;\n"
        "    font-family: 'Inter';\n"
        "    font-size: 11px;\n"
        "}\n"
        "QCheckBox::indicator {\n"
        "    width: 12px;\n"
        "    height: 12px;\n"
        "    border: 1px solid @BorderSlotBypassed;\n"
        "    background: @BgBase;\n"
        "    border-radius: 2px;\n"
        "}\n"
        "QCheckBox::indicator:checked {\n"
        "    background: @AccentGlow;\n"
        "    border-color: @AccentGlow;\n"
        "}\n";

    qss.replace("@BgBase", Color::BgBase.name());
    qss.replace("@BgSurface", Color::BgSurface.name());
    qss.replace("@BgControl", Color::BgControl.name());
    qss.replace("@TextMuted", Color::TextMuted.name());
    qss.replace("@TextPrimary", Color::TextPrimary.name());
    qss.replace("@TextSecondary", Color::TextSecondary.name());
    qss.replace("@TextLight", Color::TextLight.name());
    qss.replace("@AccentGlow", Color::AccentGlow.name());
    qss.replace("@AccentRecord", Color::AccentRecord.name());
    qss.replace("@BtnMuteActive", Color::BtnMuteActive.name());
    qss.replace("@BtnMuteText", Color::BtnMuteText.name());
    qss.replace("@BtnSoloActive", Color::BtnSoloActive.name());
    qss.replace("@BtnSoloText", Color::BtnSoloText.name());
    qss.replace("@BtnRecordText", Color::BtnRecordText.name());
    qss.replace("@BtnMonitorActive", Color::BtnMonitorActive.name());
    qss.replace("@BtnMonitorText", Color::BtnMonitorText.name());
    qss.replace("@BorderSlotBypassed", Color::BorderSlotBypassed.name());

    return qss;
}

QString Style::getMenuStyleSheet() {
    return QString(
        "/* Menu Bar */\n"
        "QMenuBar {\n"
        "    background-color: transparent;\n"
        "    color: %1;\n"
        "    border: none;\n"
        "}\n"
        "QMenuBar::item {\n"
        "    background-color: transparent;\n"
        "    padding: 8px 12px;\n"
        "    border-radius: 6px;\n"
        "    font-weight: 500;\n"
        "}\n"
        "QMenuBar::item:selected {\n"
        "    background-color: %2;\n"
        "    color: %3;\n"
        "}\n\n"
        "/* Dropdown / Context Menus */\n"
        "QMenu {\n"
        "    background-color: %4;\n"
        "    border: none;\n"
        "    border-radius: 6px;\n"
        "    padding: 6px;\n"
        "    color: %5;\n"
        "}\n"
        "QMenu::item {\n"
        "    padding: 8px 32px 8px 16px;\n"
        "    border-radius: 4px;\n"
        "    font-family: 'Inter';\n"
        "    font-size: 13px;\n"
        "    font-weight: 500;\n"
        "}\n"
        "QMenu::item:selected {\n"
        "    background-color: %2;\n"
        "    color: %3;\n"
        "}\n"
        "QMenu::item:checked {\n"
        "    background-color: %2;\n"
        "    color: %3;\n"
        "}\n"
        "QMenu::indicator {\n"
        "    image: none;\n"
        "    width: 0px;\n"
        "    height: 0px;\n"
        "}\n"
        "QMenu::item:disabled {\n"
        "    color: #565C66;\n"
        "}\n"
        "QMenu::separator {\n"
        "    height: 1px;\n"
        "    background-color: %2;\n"
        "    margin: 6px 12px;\n"
        "}\n\n"
        "/* Category Highlight Menus */\n"
        "QMenu[class=\"instrumentMenu\"]::item:selected,\n"
        "QMenu[class=\"effectMenu\"]::item:selected {\n"
        "    background-color: #E65C00; /* Premium Orange */\n"
        "    color: #FFFFFF;\n"
        "}\n"
    )
    .arg(Color::TextMuted.name())    // %1
    .arg(Color::BgControl.name())    // %2
    .arg(Color::AccentGlow.name())   // %3
    .arg(Color::BgSurface.name())    // %4
    .arg(Color::TextPrimary.name()); // %5
}

QString Style::getFieldStyleSheet() {
    return QString(
        "/* Text Inputs & Spin Boxes */\n"
        "QLineEdit, QSpinBox, QDoubleSpinBox {\n"
        "    background-color: %1;\n"
        "    border: none;\n"
        "    border-radius: 6px;\n"
        "    padding: 6px 12px;\n"
        "    color: %3;\n"
        "    font-family: 'Inter';\n"
        "    font-size: 13px;\n"
        "    font-weight: 500;\n"
        "}\n"
        "QLineEdit:hover, QLineEdit:focus,\n"
        "QSpinBox:hover, QSpinBox:focus,\n"
        "QDoubleSpinBox:hover, QDoubleSpinBox:focus {\n"
        "    background-color: %5;\n"
        "    color: %4;\n"
        "}\n\n"
        "/* BPM Selector Specific Style */\n"
        "QLineEdit#bpmSelector {\n"
        "    color: @AccentGlow;\n"
        "    font-family: 'Inter';\n"
        "    font-weight: bold;\n"
        "    font-size: 14px;\n"
        "    background-color: transparent;\n"
        "    padding: 4px 8px;\n"
        "}\n\n"
        "/* Combo Boxes */\n"
        "QComboBox {\n"
        "    background-color: %1;\n"
        "    border: none;\n"
        "    border-radius: 6px;\n"
        "    padding: 6px 24px 6px 12px;\n"
        "    color: %3;\n"
        "    font-family: 'Inter';\n"
        "    font-size: 13px;\n"
        "    font-weight: 500;\n"
        "}\n"
        "QComboBox:hover, QComboBox:focus {\n"
        "    background-color: %5;\n"
        "    color: %4;\n"
        "}\n"
        "QComboBox::drop-down {\n"
        "    subcontrol-origin: padding;\n"
        "    subcontrol-position: top right;\n"
        "    width: 24px;\n"
        "    border-left: none;\n"
        "}\n"
        "QComboBox::down-arrow {\n"
        "    width: 0px;\n"
        "    height: 0px;\n"
        "    border-left: 4px solid transparent;\n"
        "    border-right: 4px solid transparent;\n"
        "    border-top: 5px solid %2;\n"
        "    margin-right: 8px;\n"
        "}\n"
        "QComboBox::down-arrow:hover {\n"
        "    border-top-color: %4;\n"
        "}\n"
        "QComboBox QAbstractItemView {\n"
        "    background-color: %1;\n"
        "    border: none;\n"
        "    border-radius: 6px;\n"
        "    color: %3;\n"
        "    selection-background-color: #2E1E4F;\n"
        "    selection-color: %4;\n"
        "    padding: 6px;\n"
        "}\n"
    )
    .arg(Color::BgBase.name())        // %1
    .arg(Color::BgControl.name())     // %2
    .arg(Color::TextPrimary.name())   // %3
    .arg(Color::AccentGlow.name())    // %4
    .arg(Color::BgSurface.name());    // %5
}

QString Style::getListViewStyleSheet() {
    return QString(
        "/* Tree/List View Item Styling */\n"
        "QTreeView, QListView {\n"
        "    background-color: %1;\n"
        "    border: none;\n"
        "    border-radius: 6px;\n"
        "    color: %3;\n"
        "    font-family: 'Inter';\n"
        "    font-size: 13px;\n"
        "}\n"
        "QTreeView::item, QListView::item {\n"
        "    padding: 8px 6px;\n"
        "    border-radius: 4px;\n"
        "}\n"
        "QTreeView::item:hover, QListView::item:hover {\n"
        "    background-color: %4;\n"
        "    color: %5;\n"
        "}\n"
        "QTreeView::item:selected, QListView::item:selected {\n"
        "    background-color: %2;\n"
        "    color: %6;\n"
        "}\n\n"
        "/* Header View */\n"
        "QHeaderView::section {\n"
        "    background-color: %1;\n"
        "    color: %5;\n"
        "    padding: 8px 6px;\n"
        "    font-family: 'Inter';\n"
        "    font-weight: 600;\n"
        "    font-size: 12px;\n"
        "    border: none;\n"
        "    border-bottom: 1px solid %4;\n"
        "}\n"
    )
    .arg(Color::BgBase.name())      // %1
    .arg(Color::BgControl.name())   // %2
    .arg(Color::TextPrimary.name()) // %3
    .arg(Color::BgSurface.name())   // %4
    .arg(Color::TextMuted.name())   // %5
    .arg(Color::AccentGlow.name()); // %6
}

void Style::updateDynamic(QWidget* widget) {
    if (!widget) return;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

// --- High-Performance Painting Helpers ---

void PaintHelper::drawVolumetricGlow(QPainter* painter, const QRectF& rect, const QColor& color, double opacity) {
    if (!painter) return;
    
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);

    // Create a radial gradient peaking at the center bounding box
    QRadialGradient gradient(rect.center(), rect.width() / 2.0);
    
    QColor peakColor = color;
    peakColor.setAlphaF(static_cast<float>(opacity));
    
    QColor fadeColor = color;
    fadeColor.setAlphaF(0.0f);

    gradient.setColorAt(0.0, peakColor);
    
    QColor midColor = peakColor;
    midColor.setAlphaF(peakColor.alphaF() * 0.4f);
    gradient.setColorAt(0.5, midColor); // Mid decay
    
    gradient.setColorAt(1.0, fadeColor);         // Total decay at boundaries

    painter->setBrush(gradient);
    painter->drawEllipse(rect);
    painter->restore();
}

void PaintHelper::drawGlassPanel(QPainter* painter, const QRectF& rect, const QColor& baseColor, double cornerRadius) {
    if (!painter) return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Use provided base fill (respecting its alpha channel)
    QColor fill = baseColor;
    painter->setBrush(fill);

    // Subtle 1px inner border to catch light
    QColor borderHighlight(255, 255, 255, 25); // Soft white glow
    QPen pen(borderHighlight, 1.0);
    painter->setPen(pen);

    painter->drawRoundedRect(rect, cornerRadius, cornerRadius);
    painter->restore();
}

QIcon PaintHelper::createSvgIcon(const QString& resourcePath, const QSize& size) {
    QIcon icon;
    
    auto renderAndTint = [&](const QColor& color) -> QPixmap {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        
        QPainter painter(&pixmap);
        QSvgRenderer renderer(resourcePath);
        if (renderer.isValid()) {
            renderer.render(&painter);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(pixmap.rect(), color);
        }
        painter.end();
        return pixmap;
    };

    icon.addPixmap(renderAndTint(Color::TextMuted), QIcon::Normal);
    icon.addPixmap(renderAndTint(Color::TextPrimary), QIcon::Active);
    icon.addPixmap(renderAndTint(Color::AccentGlow), QIcon::Selected);
    icon.addPixmap(renderAndTint(QColor(86, 92, 102)), QIcon::Disabled);
    
    return icon;
}

void PaintHelper::drawControlGrip(QPainter* painter, const QRectF& rect, const QColor& baseColor, double cornerRadius) {
    if (!painter) return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Grip background and shadow border
    painter->setPen(QPen(Color::ControlShadow, 1.0));
    painter->setBrush(baseColor);
    painter->drawRoundedRect(rect, cornerRadius, cornerRadius);

    // Light-catching bezel highlight at top edge
    painter->setPen(QPen(Color::ControlHighlight, 1.0));
    painter->drawLine(QPointF(rect.left() + 2.0, rect.top() + 1.0),
                     QPointF(rect.right() - 2.0, rect.top() + 1.0));

    painter->restore();
}

} // namespace presentation::theme
