// src/Presentation/views/telemetry_meter.h
#pragma once

#include <QWidget>
#include <QPixmap>
#include <QMouseEvent>
#include <QDateTime>
#include "Middle Bridge/telemetry/imetering_provider.h"

namespace presentation::views {

/**
 * @brief A high-performance stereo level meter with volumetric plasma gradients and peak hold.
 * Conforms to the "Less is More" philosophy by replacing text scales with a geometric ladder.
 */
class TelemetryMeter : public QWidget {
    Q_OBJECT

public:
    explicit TelemetryMeter(QWidget* parent = nullptr);
    ~TelemetryMeter() override = default;

    QSize sizeHint() const override { return QSize(44, 120); }
    QSize minimumSizeHint() const override { return QSize(44, 120); }

    /**
     * @brief Wires the meter to the Middle Bridge for clip reset commands.
     */
    void setMeteringProvider(bridge::IMeteringProvider* provider, TrackID trackId, bool isMaster = false);

    /**
     * @brief Updates the active signal metrics. Called by the Centralized GUI scheduler.
     */
    void setLevels(float peakLeft, float peakRight, float rmsLeft, float rmsRight, bool clipLeft, bool clipRight, qint64 nowMs = 0);

    /**
     * @brief Convenience overload: updates only peak levels from a bridge::TrackUIState snapshot.
     *        RMS and clip state are approximated (rms = peak - 3 dB; clip when peak >= 0 dB).
     */
    void updatePeaks(float peakLeftdB, float peakRightdB, qint64 nowMs = 0);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    /**
     * @brief Converts a decibel level (-60.0 to +6.0) to a normalized y-coordinate ratio (0.0 to 1.0).
     */
    double dbToRatio(double db) const;

    /**
     * @brief Caches the static backing scale and dark column boundaries once.
     */
    void renderStaticBackground();

    // --- State Metrics ---
    float m_peakLeft = -120.0f;
    float m_peakRight = -120.0f;
    float m_rmsLeft = -120.0f;
    float m_rmsRight = -120.0f;
    
    bool m_clipLeft = false;
    bool m_clipRight = false;

    // --- Peak Hold Parameters ---
    float m_holdPeakLeft = -120.0f;
    float m_holdPeakRight = -120.0f;
    qint64 m_holdTimeLeft = 0;   // Timestamp of last peak transient in ms
    qint64 m_holdTimeRight = 0;

    // --- Middle Bridge Wiring ---
    bridge::IMeteringProvider* m_provider = nullptr;
    TrackID m_trackId = {};
    bool m_isMaster = false;

    // --- Background Render Cache ---
    QPixmap m_bgCache;
    bool m_bgCacheValid = false;
};

} // namespace presentation::views
