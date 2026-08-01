// src/Presentation/controllers/presentation_director.h
#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <unordered_map>
#include "common/system_primitives.h"
#include "Middle Bridge/telemetry/imetering_provider.h"
#include "Presentation/views/mixer/telemetry_meter.h"

namespace presentation::views {
class TopControlPanel;
class MixerWindow;
}

namespace presentation::controllers {

/**
 * @brief Centralized Presentation Director managing 60Hz telemetry polling and UI refreshes.
 * Implements a single unified timer driving all registered high-speed metering elements.
 */
class PresentationDirector : public QObject {
    Q_OBJECT

public:
    explicit PresentationDirector(QObject* parent = nullptr);
    ~PresentationDirector() override = default;

    /**
     * @brief Configures the Middle Bridge metering interface.
     */
    void setMeteringProvider(bridge::IMeteringProvider* provider);

    /**
     * @brief Registers an active track level meter widget.
     */
    void registerTrackMeter(TrackID trackId, views::TelemetryMeter* meter);

    /**
     * @brief Registers the master main output level meter widget.
     */
    void registerMasterMeter(views::TelemetryMeter* meter);

    /**
     * @brief Clears all registered track level meters.
     */
    void clearTrackMeters();

    /**
     * @brief Registers the Mixer Window for 60Hz frame updates.
     */
    void setMixerWindow(views::MixerWindow* mixer);

    /**
     * @brief Registers the top control panel for 60Hz toolbar updates.
     */
    void setTopControlPanel(views::TopControlPanel* panel);

    /**
     * @brief Starts the 60Hz high-resolution polling thread/timer.
     */
    void start();

    /**
     * @brief Halts the active polling timer.
     */
    void stop();

private Q_SLOTS:
    /**
     * @brief Fired at a strict 16.6ms interval to drive physical level ballistics and draw signals.
     */
    void onTimerTick();

private:
    QTimer m_timer;
    QElapsedTimer m_elapsedTimer;
    
    bridge::IMeteringProvider* m_meteringProvider = nullptr;
    std::unordered_map<TrackID, views::TelemetryMeter*> m_trackMeters;
    views::TelemetryMeter* m_masterMeter = nullptr;
    views::TopControlPanel* m_topControlPanel = nullptr;
    views::MixerWindow* m_mixerWindow = nullptr;
};

} // namespace presentation::controllers
