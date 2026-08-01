// src/Presentation/views/pianoroll_window/PianoRollWindow.h
#pragma once

#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QButtonGroup>
#include <QSplitter>
#include <QScrollBar>
#include "Middle Bridge/midi/imidi_editor_controller.h"
#include "PianoRollCanvas.h"
#include "VelocityLaneView.h"
#include "ControllerLaneView.h"
#include <unordered_map>

namespace bridge {
    class ITimelineController;
}

namespace presentation::views {

class PianoRollWindow : public QWidget {
    Q_OBJECT

public:
    explicit PianoRollWindow(bridge::IMidiEditorController* controller, QWidget* parent = nullptr);
    ~PianoRollWindow() override = default;

    // Active MIDI clip focus
    void openMidiClip(TrackID trackId, bridge::RegionID regionId);
    void setPlayheadFrame(uint64_t frame);
    void bindTimeline(bridge::ITimelineController* timeline);
    void refreshView();

private slots:
    void onToolChanged(int id);
    void onSnapChanged(int index);
    void onLaneToggle(int index);
    void onQuantizeClicked();

private:
    void setupUI();
    void applyThemeSheet();

    bridge::IMidiEditorController* controller_ = nullptr;
    bridge::ITimelineController* timeline_ = nullptr;

    // Toolbar elements
    QPushButton* btnSelect_ = nullptr;
    QPushButton* btnDraw_ = nullptr;
    QPushButton* btnErase_ = nullptr;
    QButtonGroup* toolGroup_ = nullptr;
    
    QComboBox* comboSnap_ = nullptr;
    QPushButton* btnQuantize_ = nullptr;
    
    QComboBox* comboLaneSelect_ = nullptr; // Velocity vs CC lanes

    // Splitter & sub-widgets
    QSplitter* verticalSplitter_ = nullptr;
    PianoRollCanvas* canvas_ = nullptr;
    VelocityLaneView* velocityView_ = nullptr;
    ControllerLaneView* controllerView_ = nullptr;

    uint64_t viewStartFrame_ = 0;
    uint64_t viewEndFrame_ = 44100 * 4;

    TrackID currentTrackId_ = TrackID::invalid();
    bridge::RegionID currentRegionId_ = bridge::RegionID::invalid();

    QScrollBar* vScrollBar_ = nullptr;
    QScrollBar* hScrollBar_ = nullptr;

    void updateScrollBarPositions();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    void installKeyboardShortcuts();
    int currentOctaveOffset_ = 0;
    std::unordered_map<int, bool> activeTypingKeys_;
};

} // namespace presentation::views
