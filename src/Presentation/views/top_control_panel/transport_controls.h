// src/Presentation/views/top_control_panel/transport_controls.h
#pragma once

#include <QWidget>
#include <QPushButton>
#include "Middle Bridge/timeline/itimeline_controller.h"

namespace presentation::views {

class TimeDisplay;

/**
 * @brief Transport and playback controls: mode toggle, play/stop/record, BPM, time display.
 *
 * All transport actions route exclusively through bridge::ITimelineController.
 * State is refreshed at 60Hz via updateFromBridge().
 */
class TransportControls : public QWidget {
    Q_OBJECT

public:
    explicit TransportControls(QWidget* parent = nullptr);
    ~TransportControls() override = default;

    void bind(bridge::ITimelineController* controller);
    void updateFromBridge();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setupUI();
    void onPlayClicked();
    void onStopClicked();
    void onRecordClicked();
    void onLoopToggleClicked();

    bridge::ITimelineController* m_controller = nullptr;

    QPushButton* m_loopToggle = nullptr;
    QPushButton* m_stopBtn = nullptr;
    QPushButton* m_playBtn = nullptr;
    QPushButton* m_recordBtn = nullptr;
    TimeDisplay* m_timeDisplay = nullptr;

    bool m_isPlaying = false;
    bool m_isRecording = false;
    bool m_isRecordArmed = false;
    bool m_isLoopEnabled = false;
};

} // namespace presentation::views
