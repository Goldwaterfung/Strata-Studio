// src/Presentation/views/pianoroll_window/VelocityLaneView.cpp
#include "VelocityLaneView.h"
#include "Presentation/views/theme.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>

namespace presentation::views {

VelocityLaneView::VelocityLaneView(bridge::IMidiEditorController* controller, QWidget* parent)
    : QWidget(parent), controller_(controller) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumHeight(80);
}

void VelocityLaneView::setViewportRange(uint64_t startFrame, uint64_t endFrame) {
    if (viewStartFrame_ != startFrame || viewEndFrame_ != endFrame) {
        viewStartFrame_ = startFrame;
        viewEndFrame_ = endFrame;
        update();
    }
}

void VelocityLaneView::setSelectedNoteIds(const std::vector<bridge::NoteID>& selectedIds) {
    selectedNoteIds_ = selectedIds;
    update();
}

double VelocityLaneView::frameToX(uint64_t frame) const {
    double totalFrames = static_cast<double>(viewEndFrame_ - viewStartFrame_);
    if (totalFrames <= 0.0) return KEY_WIDTH;
    double canvasWidth = static_cast<double>(width()) - KEY_WIDTH;
    double ratio = static_cast<double>(frame - viewStartFrame_) / totalFrames;
    return KEY_WIDTH + ratio * canvasWidth;
}

uint64_t VelocityLaneView::xToFrame(double x) const {
    double canvasWidth = static_cast<double>(width()) - KEY_WIDTH;
    if (canvasWidth <= 0.0) return viewStartFrame_;
    double ratio = (x - KEY_WIDTH) / canvasWidth;
    double totalFrames = static_cast<double>(viewEndFrame_ - viewStartFrame_);
    return viewStartFrame_ + static_cast<uint64_t>(std::clamp(ratio, 0.0, 1.0) * totalFrames);
}

int VelocityLaneView::hitTestVelocityBar(const QPointF& pos, const composition::MIDINote* notes, uint32_t count) const {
    for (uint32_t i = 0; i < count; ++i) {
        double x = frameToX(notes[i].startSample);
        if (std::fabs(pos.x() - x) <= 6.0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void VelocityLaneView::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 1. Draw Background
    p.fillRect(rect(), theme::Color::BgBase);

    // separator line at top
    p.setPen(QPen(theme::Color::BgControl, 1.0));
    p.drawLine(QPointF(0.0, 0.0), QPointF(static_cast<double>(width()), 0.0));

    // align piano key gray border
    p.fillRect(QRectF(0.0, 0.0, KEY_WIDTH, static_cast<double>(height())), theme::Color::BgSurface);
    p.drawLine(QPointF(KEY_WIDTH - 0.5, 0.0), QPointF(KEY_WIDTH - 0.5, static_cast<double>(height())));

    if (!controller_) return;

    composition::MIDINote notes[MAX_NOTES_BUFFER];
    uint32_t count = controller_->getNotesInViewport(viewStartFrame_, viewEndFrame_, notes, MAX_NOTES_BUFFER);
    count = std::min(count, MAX_NOTES_BUFFER);

    double h = static_cast<double>(height());

    // Draw velocity bars
    for (uint32_t i = 0; i < count; ++i) {
        double x = frameToX(notes[i].startSample);
        double valRatio = static_cast<double>(notes[i].velocity) / 127.0;
        double barY = h - valRatio * (h - 10.0);

        bool isSelected = std::find(selectedNoteIds_.begin(), selectedNoteIds_.end(), notes[i].noteId) != selectedNoteIds_.end();

        // Velocity bars
        QLinearGradient grad(QPointF(x, h), QPointF(x, barY));
        if (isSelected) {
            grad.setColorAt(0.0, theme::Color::AccentGlow);
            grad.setColorAt(1.0, theme::Color::AccentGlow);
        } else {
            grad.setColorAt(0.0, theme::Color::AccentMIDI);
            grad.setColorAt(1.0, theme::Color::AccentMIDI);
        }

        // Draw bar stems
        p.setPen(QPen(grad, 3.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(x, h), QPointF(x, barY));

        // Draw glowing cap head
        p.setPen(QPen(isSelected ? theme::Color::AccentGlow : theme::Color::AccentMIDI.lighter(110), 1.0));
        p.setBrush(isSelected ? theme::Color::AccentGlow : theme::Color::AccentMIDI);
        p.drawEllipse(QRectF(x - 3.5, barY - 3.5, 7.0, 7.0));
    }
}

void VelocityLaneView::mousePressEvent(QMouseEvent* event) {
    if (!controller_) return;

    QPointF pos = event->position();
    
    composition::MIDINote notes[MAX_NOTES_BUFFER];
    uint32_t count = controller_->getNotesInViewport(viewStartFrame_, viewEndFrame_, notes, MAX_NOTES_BUFFER);
    count = std::min(count, MAX_NOTES_BUFFER);

    int hitIdx = hitTestVelocityBar(pos, notes, count);
    if (hitIdx >= 0) {
        isDragging_ = true;
        activeDragNoteId_ = notes[hitIdx].noteId;
        
        // Scale velocity based on mouse Y position
        double h = static_cast<double>(height());
        double ratio = (h - pos.y()) / (h - 10.0);
        uint8_t newVel = static_cast<uint8_t>(std::clamp(ratio * 127.0, 0.0, 127.0));

        controller_->setNoteVelocity(activeDragNoteId_, newVel);
        emit velocitiesMutated();
        update();
    }
}

void VelocityLaneView::mouseMoveEvent(QMouseEvent* event) {
    if (isDragging_ && controller_) {
        QPointF pos = event->position();
        double h = static_cast<double>(height());
        double ratio = (h - pos.y()) / (h - 10.0);
        uint8_t newVel = static_cast<uint8_t>(std::clamp(ratio * 127.0, 0.0, 127.0));

        controller_->setNoteVelocity(activeDragNoteId_, newVel);
        emit velocitiesMutated();
        update();
    }
}

void VelocityLaneView::mouseReleaseEvent(QMouseEvent* /*event*/) {
    isDragging_ = false;
    activeDragNoteId_ = {0, 0};
}

} // namespace presentation::views
