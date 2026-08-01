// src/Presentation/views/pianoroll_window/PianoRollCanvas.h
#pragma once

#include <QWidget>
#include <QPointF>
#include <QRectF>
#include <vector>
#include "Middle Bridge/midi/imidi_editor_controller.h"

namespace bridge {
    class ITimelineController;
}

namespace presentation::views {

enum class PianoRollTool : uint8_t {
    Select = 0,
    Draw   = 1,
    Erase  = 2,
};

class PianoRollCanvas : public QWidget {
    Q_OBJECT

public:
    explicit PianoRollCanvas(bridge::IMidiEditorController* controller, QWidget* parent = nullptr);
    ~PianoRollCanvas() override = default;

    // Viewport configuration
    void setViewportRange(uint64_t startFrame, uint64_t endFrame);
    void setPitchRange(uint8_t minPitch, uint8_t maxPitch);
    void setPlayheadFrame(uint64_t frame);
    void setSnapResolution(uint32_t resolutionTicks);
    void setTool(PianoRollTool tool);
    void triggerQuantize();
    void bindTimeline(bridge::ITimelineController* timeline);

    // Getters for scrollbars
    uint8_t getMinPitch() const { return minPitch_; }
    uint8_t getMaxPitch() const { return maxPitch_; }

    // Selection & Note operations
    const std::vector<bridge::NoteID>& getSelectedNoteIds() const { return selectedNoteIds_; }
    void clearSelection();
    void selectAll();
    void splitNotesAtPlayhead(uint64_t playheadFrame);
    void duplicateSelection();
    void toggleMuteSelection();
    void glueSelection();
    void forceLegatoSelection();
    void invertSelection();
    void adjustVelocity(int delta);

signals:
    void selectionChanged();
    void notesMutated();
    void viewportRangeChanged(uint64_t startFrame, uint64_t endFrame);
    void pitchRangeChanged(uint8_t minPitch, uint8_t maxPitch);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // Coordinate conversion
    double frameToX(uint64_t frame) const;
    uint64_t xToFrame(double x) const;
    double pitchToY(uint8_t pitch) const;
    uint8_t yToPitch(double y) const;
    QRectF noteToRect(const composition::MIDINote& note) const;

    // Drawing helpers
    void drawGrid(QPainter& p);
    void drawPianoKeys(QPainter& p);
    void drawNotes(QPainter& p);
    void drawSelectionRect(QPainter& p);
    void drawPlayhead(QPainter& p);

    // Hit testing
    int hitTestNote(const QPointF& pos, composition::MIDINote* notes, uint32_t count, bool& outOnEdge) const;

    // Snap helpers
    uint64_t snapFrame(uint64_t frame) const;

    bridge::IMidiEditorController* controller_ = nullptr;
    bridge::ITimelineController* timeline_ = nullptr;

    // Viewport state
    uint64_t viewStartFrame_ = 0;
    uint64_t viewEndFrame_ = 44100 * 4; // default 4 seconds
    uint8_t minPitch_ = 36;             // Default C2
    uint8_t maxPitch_ = 84;             // Default C6
    uint64_t playheadFrame_ = 0;
    uint32_t snapResolutionTicks_ = 120; // Default 16th note at 120 BPM / 480 PPQ
    PianoRollTool activeTool_ = PianoRollTool::Select;

    // Geometry sizes
    static constexpr double KEY_WIDTH = 48.0;
    double noteHeight_ = 20.0;

    // Drag state machine
    enum class DragState {
        Idle,
        SelectRubberBand,
        MovingNote,
        ResizingNote,
        DrawingNote
    };

    DragState dragState_ = DragState::Idle;
    QPointF dragAnchorPos_;
    QRectF rubberBandRect_;
    bool rubberBandVisible_ = false;

    // Active drag details
    bridge::NoteID activeDragNoteId_ = {0, 0};
    uint64_t dragStartFrame_ = 0;
    uint8_t dragStartPitch_ = 0;
    uint64_t dragOrigStartFrame_ = 0;
    uint64_t dragOrigDuration_ = 0;
    uint8_t dragOrigPitch_ = 0;

    // Selection
    std::vector<bridge::NoteID> selectedNoteIds_;

    // Live virtual keyboard state
    uint8_t activeClickedPitch_ = 0;
    bool isPianoKeyActive_ = false;

    // Constant buffer limit
    static constexpr uint32_t MAX_NOTES_BUFFER = 1024;

    // Scroll state & constants
    double verticalScrollAccumulator_ = 0.0;
    static constexpr double VERTICAL_SCROLL_THRESHOLD = 120.0;
};

} // namespace presentation::views
