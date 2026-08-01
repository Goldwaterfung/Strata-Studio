// src/Presentation/views/theme.h
#pragma once

#include <QColor>
#include <QFont>
#include <QString>
#include <QPainter>
#include <QRectF>
#include <QIcon>
#include <QSize>

class QWidget;

namespace presentation::theme {

/**
 * @brief Color Palette Tokens representing our Deep Cyber-Industrial aesthetic
 */
struct Color {
    static const QColor BgBase;        // Shell Background (#161616)
    static const QColor BgSurface;     // Panels, Tracks, Strips (#222222)
    static const QColor BgControl;     // Inactive Buttons, Slots (#323232)
    static const QColor AccentGlow;    // Soft Violet Glow (#A78BFA)
    static const QColor AccentRecord;  // Armed / Recording State (#FF3B30)
    static const QColor AccentMIDI;    // MIDI/Synth Events Amethyst (#A855F7)
    static const QColor AccentAudio;   // Audio Ocean Cobalt (#3B82F6)
    static const QColor TextPrimary;   // Active / Highly Legible Text (#E8E8E8)
    static const QColor TextMuted;     // Grids, Secondary Labels (#888888)

    // Centralized Alert and Grid Tokens
    static const QColor BgAlertMuted;      // Muted Alert Background (#1A0D0D)
    static const QColor BorderAlertMuted;  // Border Alert Muted (#331A1A)
    static const QColor BorderAlert;       // Accent Alert Border (#FF5555)
    static const QColor GridOverlay;       // Subtle Grid Overlay (#FFFFFF with 6/255 alpha)

    // Button state colors (track control: mute, solo, record, monitor)
    static const QColor BtnMuteActive;     // Amber active state (#D4A84B)
    static const QColor BtnMuteText;       // Text on mute button (#161616)
    static const QColor BtnSoloActive;     // Jade Green active state (#2DA87E)
    static const QColor BtnSoloText;       // Text on solo button (#161616)
    static const QColor BtnRecordText;     // Text on record button (#E8E8E8)
    static const QColor BtnMonitorActive;  // Blue active state (#3B82F6)
    static const QColor BtnMonitorText;    // Text on monitor button (#E8E8E8)

    // Slot backgrounds and borders
    static const QColor BgSlotEmpty;         // Empty slot background (#1C1E24)
    static const QColor BorderSlotEmpty;     // Empty slot border (#2B303C)
    static const QColor BgSlotBypassed;      // Bypassed slot background (#2C323F)
    static const QColor BorderSlotBypassed;  // Bypassed slot border (#3F4758)
    static const QColor BgSlotActive;        // Active slot background (#3A4454)
    static const QColor BorderSlotActive;    // Active slot border (#505E78)

    // Plugin active and bypassed states
    static const QColor PluginActive;        // Active plugin LED/label (#E65C00)
    static const QColor PluginBypassed;      // Bypassed plugin LED/label (#7E8A9F)

    // Auxiliary sends active states
    static const QColor SendPreFader;        // Pre-fader send active teal (#00D2B4)
    static const QColor SendPostFader;       // Post-fader send active blue (#5096FF)

    // Telemetry safety metrics
    static const QColor SafetyAmber;         // Amber unity line (#FFB300 / rgb(255, 179, 0))
    static const QColor PeakHoldWhite;       // Transient Peak Hold lines (rgb(255, 255, 255, 200))
    static const QColor ClipIndicatorOff;    // Deep dim warning indicator (#281416 / rgb(40, 20, 22))

    // Extended text tokens
    static const QColor TextSecondary;       // Muted silver/blue-gray (#9DB2BF)
    static const QColor TextLight;           // Soft light white (#DDE6ED)

    // Fader & Knob overlays
    static const QColor ControlShadow;       // Grip shadows (rgb(0, 0, 0, 160))
    static const QColor ControlHighlight;    // Grip bezel highlight (rgb(255, 255, 255, 30))
};

/**
 * @brief Standardized layout spacing and size configuration constants.
 */
struct Layout {
    static constexpr int TrackStripWidth = 140;
    static constexpr int MasterStripWidth = 170;

    // Track & Sub-Lane Height Limits (Pixels)
    static constexpr int MinTrackHeight       = 24;
    static constexpr int MaxTrackHeight       = 256;
    static constexpr int DefaultTrackHeight   = 72;

    static constexpr int MinSubLaneHeight     = 24;
    static constexpr int DefaultSubLaneHeight = 60;
    static constexpr int MaxSubLaneHeight     = 200;

    // Plugin Editor Layout Constants
    static constexpr int PluginTweakedBarHeight   = 28;
    static constexpr int PluginHeaderBtnHeight    = 24;
    static constexpr int PluginSidechainBarHeight = 32;
};

/**
 * @brief Centralized typography helper returning the unified Inter font.
 */
class Font {
public:
    static constexpr int SizeTiny = 8;
    static constexpr int SizeDetail = 9;
    static constexpr int SizeSecondary = 10;
    static constexpr int SizePrimary = 11;
    static constexpr int SizeHeader = 14;
    static constexpr int SizeTitle = 22;

    /**
     * @brief Registers the custom application fonts (Inter) with QFontDatabase.
     */
    static void initialize();

    /**
     * @brief Primary UI font (Inter) for labels, menus, and standard text.
     */
    static QFont primary(int pointSize = 11, int weight = QFont::Normal, double letterSpacingPercent = 100.0);

    /**
     * @brief Fixed-width style numerical font (Inter) to prevent layout shifts.
     */
    static QFont monospace(int pointSize = 11, int weight = QFont::Normal, double letterSpacingPercent = 100.0);
};

/**
 * @brief Global QSS Stylesheet for base containers, panels, splitters, and docks.
 */
class Style {
public:
    /**
     * @brief Generates the global Cyber-Industrial QSS styling rules.
     */
    static QString getGlobalStyleSheet();

    // Modular stylesheet segments
    static QString getCoreStyleSheet();
    static QString getButtonStyleSheet();
    static QString getMenuStyleSheet();
    static QString getFieldStyleSheet();
    static QString getListViewStyleSheet();

    /**
     * @brief Forces Qt to re-evaluate and apply dynamic stylesheet changes on a widget.
     */
    static void updateDynamic(QWidget* widget);
};

/**
 * @brief High-performance hardware-optimized vector painting helpers for custom widgets
 */
class PaintHelper {
public:
    /**
     * @brief Paints a volumetric, neon glow around an element (e.g., active clip light or knob needle)
     * using a radial gradient.
     */
    static void drawVolumetricGlow(QPainter* painter, const QRectF& rect, const QColor& color, double opacity = 0.4);

    /**
     * @brief Paints a sleek, dark glassmorphic panel with high-contrast borders.
     */
    static void drawGlassPanel(QPainter* painter, const QRectF& rect, const QColor& baseColor, double cornerRadius = 4.0);

    /**
     * @brief Creates a QIcon from an SVG resource, tinted for Normal, Active, Selected, and Disabled states.
     */
    static QIcon createSvgIcon(const QString& resourcePath, const QSize& size = QSize(20, 20));

    /**
     * @brief Paints a 3D anodized grip handle with bevel highlights and shadows.
     */
    static void drawControlGrip(QPainter* painter, const QRectF& rect, const QColor& baseColor, double cornerRadius = 3.0);
};

} // namespace presentation::theme
