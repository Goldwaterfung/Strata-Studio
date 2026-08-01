// src/Presentation/controllers/presentation_director.cpp
#include "presentation_director.h"
#include "Presentation/views/top_control_panel/top_control_panel.h"
#include "Presentation/views/mixer/mixer_window.h"
#include <QDateTime>

namespace presentation::controllers {

PresentationDirector::PresentationDirector(QObject* parent)
    : QObject(parent) {
    // 60Hz tick interval (approx 16.67ms)
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &PresentationDirector::onTimerTick);
}

void PresentationDirector::setMeteringProvider(bridge::IMeteringProvider* provider) {
    m_meteringProvider = provider;
}

void PresentationDirector::registerTrackMeter(TrackID trackId, views::TelemetryMeter* meter) {
    if (!meter) return;
    m_trackMeters[trackId] = meter;
    
    // Wire the level meter back to the metering provider for resetting clip flags
    if (m_meteringProvider) {
        meter->setMeteringProvider(m_meteringProvider, trackId);
    }
}

void PresentationDirector::registerMasterMeter(views::TelemetryMeter* meter) {
    if (!meter) return;
    m_masterMeter = meter;
    
    if (m_meteringProvider) {
        // Master Bus maps to reserved TrackID 0
        meter->setMeteringProvider(m_meteringProvider, TrackID{0, 0}, true);
    }
}

void PresentationDirector::clearTrackMeters() {
    m_trackMeters.clear();
}

void PresentationDirector::setMixerWindow(views::MixerWindow* mixer) {
    m_mixerWindow = mixer;
}

void PresentationDirector::setTopControlPanel(views::TopControlPanel* panel) {
    m_topControlPanel = panel;
}

void PresentationDirector::start() {
    m_elapsedTimer.start();
    m_timer.start();
}

void PresentationDirector::stop() {
    m_timer.stop();
}

void PresentationDirector::onTimerTick() {
    if (!m_meteringProvider) return;

    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    // Calculate real elapsed delta-time in ms to keep level ballistics completely frame-rate independent
    double elapsedMs = 16.67;
    if (m_elapsedTimer.isValid()) {
        elapsedMs = static_cast<double>(m_elapsedTimer.restart());
    } else {
        m_elapsedTimer.start();
    }

    // 1. Trigger ballistics decay filter algorithms in Middle Bridge (saves RT thread CPU overhead)
    m_meteringProvider->updateMeters(elapsedMs);

    // 2. Poll and dispatch stereo level metrics to all active channels
    for (auto const& [trackId, meter] : m_trackMeters) {
        bridge::MeterLevel level = m_meteringProvider->getTrackLevels(trackId);
        meter->setLevels(level.peakLeft, level.peakRight, level.rmsLeft, level.rmsRight, level.clipLeft, level.clipRight, nowMs);
    }

    // 3. Poll and dispatch to master meter
    if (m_masterMeter) {
        bridge::MeterLevel level = m_meteringProvider->getMasterLevels();
        m_masterMeter->setLevels(level.peakLeft, level.peakRight, level.rmsLeft, level.rmsRight, level.clipLeft, level.clipRight, nowMs);
    }

    // 4. Update top control panel (transport state, time display, input modes)
    if (m_topControlPanel) {
        m_topControlPanel->updateFromBridge();
    }

    // 5. Update mixer window states
    if (m_mixerWindow) {
        m_mixerWindow->updateFromBridge();
    }
}

} // namespace presentation::controllers
