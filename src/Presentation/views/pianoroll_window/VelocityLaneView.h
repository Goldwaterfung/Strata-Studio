// src/Presentation/views/pianoroll_window/VelocityLaneView.h
#pragma once

#include <QWidget>
#include <vector>
#include "Middle Bridge/midi/imidi_editor_controller.h"

namespace presentation::views {

class VelocityLaneView : public QWidget {
    Q_OBJECT

public:
    explicit VelocityLaneView(bridge::IMidiEditorController* controller, QWidget* parent = nullptr);
    ~VelocityLaneView() override = default;

    void setViewportRange(uint64_t startFrame, uint64_t endFrame);
    void setSelectedNoteIds(const std::vector<bridge::NoteID>& selectedIds);

signals:
    void velocitiesMutated();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    double frameToX(uint64_t frame) const;
    uint64_t xToFrame(double x) const;

    // Hit testing
    int hitTestVelocityBar(const QPointF& pos, const composition::MIDINote* notes, uint32_t count) const;

    bridge::IMidiEditorController* controller_ = nullptr;

    uint64_t viewStartFrame_ = 0;
    uint64_t viewEndFrame_ = 44100 * 4;
    std::vector<bridge::NoteID> selectedNoteIds_;

    // Constant offset to align with canvas keys
    static constexpr double KEY_WIDTH = 48.0;
    static constexpr uint32_t MAX_NOTES_BUFFER = 1024;

    bool isDragging_ = false;
    bridge::NoteID activeDragNoteId_ = {0, 0};
};

} // namespace presentation::views
