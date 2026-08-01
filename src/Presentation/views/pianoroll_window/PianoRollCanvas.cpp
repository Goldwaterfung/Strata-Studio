// src/Presentation/views/pianoroll_window/PianoRollCanvas.cpp
#include "PianoRollCanvas.h"
#include "Presentation/views/theme.h"
#include "Middle Bridge/timeline/itimeline_controller.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>

namespace presentation::views {

PianoRollCanvas::PianoRollCanvas(bridge::IMidiEditorController* controller, QWidget* parent)
    : QWidget(parent), controller_(controller) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void PianoRollCanvas::setViewportRange(uint64_t startFrame, uint64_t endFrame) {
    if (viewStartFrame_ != startFrame || viewEndFrame_ != endFrame) {
        viewStartFrame_ = startFrame;
        viewEndFrame_ = endFrame;
        update();
        emit viewportRangeChanged(viewStartFrame_, viewEndFrame_);
    }
}

void PianoRollCanvas::setPitchRange(uint8_t minPitch, uint8_t maxPitch) {
    if (minPitch_ != minPitch || maxPitch_ != maxPitch) {
        minPitch_ = minPitch;
        maxPitch_ = maxPitch;
        update();
        emit pitchRangeChanged(minPitch_, maxPitch_);
    }
}

void PianoRollCanvas::setPlayheadFrame(uint64_t frame) {
    if (playheadFrame_ != frame) {
        playheadFrame_ = frame;
        update();
    }
}

void PianoRollCanvas::bindTimeline(bridge::ITimelineController* timeline) {
    timeline_ = timeline;
    update();
}

void PianoRollCanvas::setSnapResolution(uint32_t resolutionTicks) {
    snapResolutionTicks_ = resolutionTicks;
}

void PianoRollCanvas::setTool(PianoRollTool tool) {
    activeTool_ = tool;
}

void PianoRollCanvas::triggerQuantize() {
    if (!controller_ || selectedNoteIds_.empty()) return;
    controller_->quantizeSelectedNotes(
        selectedNoteIds_.data(),
        static_cast<uint32_t>(selectedNoteIds_.size()),
        static_cast<uint16_t>(snapResolutionTicks_),
        1.0f,  // 100% strength
        false, // Quantize start only
        50     // 50% swing (none)
    );
    emit notesMutated();
    update();
}

void PianoRollCanvas::clearSelection() {
    selectedNoteIds_.clear();
    emit selectionChanged();
    update();
}

void PianoRollCanvas::selectAll() {
    if (!controller_) return;
    std::vector<composition::MIDINote> notes(4096);
    uint32_t count = controller_->getNotesInViewport(0, UINT64_MAX, notes.data(), static_cast<uint32_t>(notes.size()));
    if (count == 0) return;

    selectedNoteIds_.clear();
    selectedNoteIds_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        selectedNoteIds_.push_back(notes[i].noteId);
    }
    emit selectionChanged();
    update();
}

double PianoRollCanvas::frameToX(uint64_t frame) const {
    double totalFrames = static_cast<double>(viewEndFrame_ - viewStartFrame_);
    if (totalFrames <= 0.0) return KEY_WIDTH;
    double canvasWidth = static_cast<double>(width()) - KEY_WIDTH;
    double ratio = static_cast<double>(frame - viewStartFrame_) / totalFrames;
    return KEY_WIDTH + ratio * canvasWidth;
}

uint64_t PianoRollCanvas::xToFrame(double x) const {
    double canvasWidth = static_cast<double>(width()) - KEY_WIDTH;
    if (canvasWidth <= 0.0) return viewStartFrame_;
    double ratio = (x - KEY_WIDTH) / canvasWidth;
    double totalFrames = static_cast<double>(viewEndFrame_ - viewStartFrame_);
    return viewStartFrame_ + static_cast<uint64_t>(std::clamp(ratio, 0.0, 1.0) * totalFrames);
}

double PianoRollCanvas::pitchToY(uint8_t pitch) const {
    if (pitch < minPitch_ || pitch > maxPitch_) return -100.0;
    double totalPitches = static_cast<double>(maxPitch_ - minPitch_ + 1);
    double canvasHeight = static_cast<double>(height());
    double pitchIndex = static_cast<double>(maxPitch_ - pitch);
    return (pitchIndex / totalPitches) * canvasHeight;
}

uint8_t PianoRollCanvas::yToPitch(double y) const {
    double canvasHeight = static_cast<double>(height());
    if (canvasHeight <= 0.0) return minPitch_;
    double totalPitches = static_cast<double>(maxPitch_ - minPitch_ + 1);
    double ratio = y / canvasHeight;
    int32_t pitchVal = maxPitch_ - static_cast<int32_t>(std::floor(ratio * totalPitches));
    return static_cast<uint8_t>(std::clamp(pitchVal, static_cast<int32_t>(minPitch_), static_cast<int32_t>(maxPitch_)));
}

QRectF PianoRollCanvas::noteToRect(const composition::MIDINote& note) const {
    double x1 = frameToX(note.startSample);
    double x2 = frameToX(note.endSample);
    double y = pitchToY(note.pitch);
    double totalPitches = static_cast<double>(maxPitch_ - minPitch_ + 1);
    double h = static_cast<double>(height()) / totalPitches;
    return QRectF(x1, y + 1.0, std::max(2.0, x2 - x1), h - 2.0);
}

uint64_t PianoRollCanvas::snapFrame(uint64_t frame) const {
    if (snapResolutionTicks_ == 0) return frame;
    if (timeline_) {
        const uint64_t ticks = timeline_->samplesToTicks(frame);
        const uint64_t low = (ticks / snapResolutionTicks_) * snapResolutionTicks_;
        const uint64_t high = low + snapResolutionTicks_;
        const uint64_t snappedTicks = (ticks - low < high - ticks) ? low : high;
        return timeline_->ticksToSamples(snappedTicks);
    }
    // Fallback if no timeline: 16th note default fallback
    uint64_t gridSamples = 44100 / 4; // roughly 16th note at 120 bpm
    return (frame / gridSamples) * gridSamples;
}

void PianoRollCanvas::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 1. Draw Grid
    drawGrid(p);

    // 2. Draw Piano Roll Keys
    drawPianoKeys(p);

    // 3. Draw Notes
    drawNotes(p);

    // 4. Draw Selection rubber band
    if (rubberBandVisible_) {
        drawSelectionRect(p);
    }

    // 5. Draw Playhead
    drawPlayhead(p);
}

void PianoRollCanvas::drawGrid(QPainter& p) {
    p.fillRect(rect(), theme::Color::BgBase);

    double totalPitches = static_cast<double>(maxPitch_ - minPitch_ + 1);
    double h = static_cast<double>(height()) / totalPitches;
    double canvasWidth = static_cast<double>(width());

    // Draw horizontal lane backgrounds
    for (uint8_t pitch = minPitch_; pitch <= maxPitch_; ++pitch) {
        double y = pitchToY(pitch);
        uint8_t noteMod = pitch % 12;
        bool isBlackKey = (noteMod == 1 || noteMod == 3 || noteMod == 6 || noteMod == 8 || noteMod == 10);
        
        QColor rowColor = isBlackKey ? QColor(0x15, 0x17, 0x1D, 255) : QColor(0x1B, 0x1E, 0x24, 255);
        p.fillRect(QRectF(KEY_WIDTH, y, canvasWidth - KEY_WIDTH, h), rowColor);

        // Divider line
        p.setPen(QPen(QColor(0x23, 0x27, 0x30, 100), 1.0));
        p.drawLine(QPointF(KEY_WIDTH, y + h - 0.5), QPointF(canvasWidth, y + h - 0.5));
    }

    // Draw vertical grid lines (bars & beats)
    double totalFrames = static_cast<double>(viewEndFrame_ - viewStartFrame_);
    if (totalFrames > 0.0) {
        if (timeline_) {
            uint32_t startBar, startBeat, startTick;
            timeline_->frameToBBT(viewStartFrame_, startBar, startBeat, startTick);

            // Estimate frames per bar at start to determine barStep density
            uint64_t currentBarFrame = timeline_->bbtToFrame(startBar, 1, 0);
            uint64_t nextBarFrame = timeline_->bbtToFrame(startBar + 1, 1, 0);
            double estFramesPerBar = static_cast<double>(nextBarFrame > currentBarFrame ? nextBarFrame - currentBarFrame : 48000.0 * 2.0);

            double zoomFactor = (canvasWidth - KEY_WIDTH) / totalFrames;
            uint32_t barStep = 1;
            while (estFramesPerBar * static_cast<double>(barStep) * zoomFactor < 40.0) {
                barStep *= 2;
            }

            const double widgetH = static_cast<double>(height());

            for (uint32_t bar = startBar; ; bar += barStep) {
                uint64_t barFrame = timeline_->bbtToFrame(bar, 1, 0);
                if (barFrame > viewEndFrame_) {
                    break;
                }

                if (barFrame >= viewStartFrame_) {
                    const double x = frameToX(barFrame);
                    p.setPen(QPen(QColor(0x38, 0x3D, 0x4A, 200), 1.0)); // Lighter gray for bar lines
                    p.drawLine(QPointF(x, 0.0), QPointF(x, widgetH));
                }

                // Only draw beats if we're not skipping bars
                if (barStep == 1) {
                    uint8_t num, den;
                    timeline_->getTimeSignatureAtFrame(barFrame, num, den);
                    
                    // Check spacing of a beat to see if it's too packed
                    uint64_t nextBeatFrame = timeline_->bbtToFrame(bar, 2, 0);
                    double beatWidthPx = static_cast<double>(nextBeatFrame > barFrame ? nextBeatFrame - barFrame : 0) * zoomFactor;

                    if (beatWidthPx >= 12.0) {
                        for (uint32_t beat = 2; beat <= num; ++beat) {
                            uint64_t beatFrame = timeline_->bbtToFrame(bar, beat, 0);
                            if (beatFrame > viewEndFrame_) {
                                break;
                            }
                            if (beatFrame >= viewStartFrame_) {
                                const double x = frameToX(beatFrame);
                                p.setPen(QPen(QColor(0x2C, 0x30, 0x3B, 120), 1.0)); // Subtler beat lines
                                p.drawLine(QPointF(x, 0.0), QPointF(x, widgetH));
                            }
                        }
                    }
                }
            }
        } else {
            // Fallback if no timeline
            double barWidthPx = 160.0; // target width for beat markers
            double framesPerLine = totalFrames * barWidthPx / (canvasWidth - KEY_WIDTH);
            double gridStep = 44100.0; // 1 second chunks for base beat
            while (gridStep < framesPerLine) gridStep *= 2.0;

            uint64_t firstLine = (viewStartFrame_ / static_cast<uint64_t>(gridStep) + 1) * static_cast<uint64_t>(gridStep);
            p.setPen(QPen(QColor(0x2C, 0x30, 0x3B, 180), 1.0));
            
            for (uint64_t frame = firstLine; frame < viewEndFrame_; frame += static_cast<uint64_t>(gridStep)) {
                double x = frameToX(frame);
                p.drawLine(QPointF(x, 0.0), QPointF(x, static_cast<double>(height())));
            }
        }
    }
}

void PianoRollCanvas::drawPianoKeys(QPainter& p) {
    double totalPitches = static_cast<double>(maxPitch_ - minPitch_ + 1);
    double h = static_cast<double>(height()) / totalPitches;

    p.fillRect(QRectF(0.0, 0.0, KEY_WIDTH, static_cast<double>(height())), theme::Color::BgSurface);

    // Outlines and key details
    for (uint8_t pitch = minPitch_; pitch <= maxPitch_; ++pitch) {
        double y = pitchToY(pitch);
        uint8_t noteMod = pitch % 12;
        bool isBlackKey = (noteMod == 1 || noteMod == 3 || noteMod == 6 || noteMod == 8 || noteMod == 10);
        bool isPressed = (isPianoKeyActive_ && pitch == activeClickedPitch_);

        if (isPressed) {
            // Sleek high-tech cyber-teal gradient highlight for the pressed key
            QLinearGradient pressGrad(0.0, y, KEY_WIDTH, y);
            pressGrad.setColorAt(0.0, theme::Color::AccentMIDI);
            pressGrad.setColorAt(1.0, theme::Color::AccentGlow);
            p.fillRect(QRectF(0.0, y, KEY_WIDTH, h), pressGrad);
        } else if (isBlackKey) {
            p.fillRect(QRectF(0.0, y + 1.0, KEY_WIDTH - 12.0, h - 2.0), QColor(0x0C, 0x0D, 0x10));
        } else {
            p.setPen(QPen(QColor(0x2C, 0x30, 0x3B, 255), 1.0));
            p.drawLine(QPointF(0.0, y + h - 0.5), QPointF(KEY_WIDTH, y + h - 0.5));
        }

        // Draw key labels on white keys with Level of Detail (LOD)
        if (!isBlackKey) {
            bool shouldDraw = false;
            QString labelText;
            int octave = static_cast<int>(pitch) / 12 - 1;

            if (h >= 24.0) {
                // High detail: Show all white keys with octave numbers
                static const char* kWhiteNames[] = {
                    "C", "", "D", "", "E", "F", "", "G", "", "A", "", "B"
                };
                shouldDraw = true;
                labelText = QString("%1%2").arg(kWhiteNames[noteMod]).arg(octave);
            } else if (h >= 16.0) {
                // Medium detail: Show C and G keys with octave numbers
                if (noteMod == 0) {
                    shouldDraw = true;
                    labelText = QString("C%1").arg(octave);
                } else if (noteMod == 7) {
                    shouldDraw = true;
                    labelText = QString("G%1").arg(octave);
                }
            } else {
                // Low detail (h < 16.0): Show only C keys with octave numbers
                if (noteMod == 0) {
                    shouldDraw = true;
                    labelText = QString("C%1").arg(octave);
                }
            }

            if (shouldDraw && !labelText.isEmpty()) {
                p.save();
                p.setFont(theme::Font::primary(8, QFont::DemiBold));
                p.setPen(isPressed ? theme::Color::BgBase : theme::Color::TextMuted);
                QRectF textRect(0.0, y, KEY_WIDTH - 6.0, h);
                p.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, labelText);
                p.restore();
            }
        }
    }

    // Border edge
    p.setPen(QPen(QColor(0x23, 0x27, 0x30, 255), 1.5));
    p.drawLine(QPointF(KEY_WIDTH - 0.5, 0.0), QPointF(KEY_WIDTH - 0.5, static_cast<double>(height())));
}

void PianoRollCanvas::drawNotes(QPainter& p) {
    if (!controller_) return;

    composition::MIDINote notes[MAX_NOTES_BUFFER];
    uint32_t count = controller_->getNotesInViewport(viewStartFrame_, viewEndFrame_, notes, MAX_NOTES_BUFFER);
    count = std::min(count, MAX_NOTES_BUFFER);

    for (uint32_t i = 0; i < count; ++i) {
        QRectF r = noteToRect(notes[i]);
        if (r.width() < 1.0) continue;

        bool isSelected = std::find(selectedNoteIds_.begin(), selectedNoteIds_.end(), notes[i].noteId) != selectedNoteIds_.end();

        // Gorgeous glassmorphic gradient fills
        QLinearGradient grad(r.topLeft(), r.bottomLeft());
        if (isSelected) {
            grad.setColorAt(0.0, theme::Color::AccentGlow);
            grad.setColorAt(1.0, theme::Color::AccentMIDI);
        } else {
            grad.setColorAt(0.0, theme::Color::AccentMIDI);
            grad.setColorAt(1.0, QColor(0x7C, 0x3A, 0xED)); // darker amethyst
        }

        p.setPen(isSelected ? QPen(theme::Color::AccentGlow, 1.5) : QPen(QColor(0x90, 0x60, 0xFA), 1.0));
        p.setBrush(grad);
        p.drawRoundedRect(r, 3.0, 3.0);

        // Volumetric inner gloss
        theme::PaintHelper::drawVolumetricGlow(&p, r, isSelected ? theme::Color::AccentGlow : theme::Color::AccentMIDI, 0.2);
    }
}

void PianoRollCanvas::drawSelectionRect(QPainter& p) {
    p.setPen(QPen(theme::Color::AccentGlow, 1.0, Qt::DashLine));
    QColor selectionBrushColor = theme::Color::AccentGlow;
    selectionBrushColor.setAlpha(25);
    p.setBrush(selectionBrushColor);
    p.drawRect(rubberBandRect_);
}

void PianoRollCanvas::drawPlayhead(QPainter& p) {
    double x = frameToX(playheadFrame_);
    if (x < KEY_WIDTH || x > static_cast<double>(width())) return;

    double h = static_cast<double>(height());

    // Halo glow
    QLinearGradient glow(x - 3.0, 0.0, x + 3.0, 0.0);
    glow.setColorAt(0.0, QColor(0xFF, 0x3B, 0x30, 0));
    glow.setColorAt(0.5, QColor(0xFF, 0x3B, 0x30, 60));
    glow.setColorAt(1.0, QColor(0xFF, 0x3B, 0x30, 0));
    p.fillRect(QRectF(x - 3.0, 0.0, 6.0, h), glow);

    // Solid line
    p.setPen(QPen(QColor(0xFF, 0x3B, 0x30, 200), 1.0));
    p.drawLine(QPointF(x, 0.0), QPointF(x, h));
}

int PianoRollCanvas::hitTestNote(const QPointF& pos, composition::MIDINote* notes, uint32_t count, bool& outOnEdge) const {
    outOnEdge = false;
    for (int32_t i = static_cast<int32_t>(count) - 1; i >= 0; --i) {
        QRectF r = noteToRect(notes[i]);
        if (r.contains(pos)) {
            // Check if within 6 px of the right edge (for resizing)
            if (r.right() - pos.x() <= 6.0) {
                outOnEdge = true;
            }
            return i;
        }
    }
    return -1;
}

void PianoRollCanvas::mousePressEvent(QMouseEvent* event) {
    if (!controller_) return;

    QPointF pos = event->position();
    dragAnchorPos_ = pos;

    // Check if clicked inside the vertical piano keys area on the left
    if (pos.x() < KEY_WIDTH) {
        uint8_t pitch = yToPitch(pos.y());
        activeClickedPitch_ = pitch;
        isPianoKeyActive_ = true;

        // Trigger real-time note-on
        controller_->noteOn(pitch, 100, 0);
        update();
        return; // Bypass grid actions
    }

    composition::MIDINote notes[MAX_NOTES_BUFFER];
    uint32_t count = controller_->getNotesInViewport(viewStartFrame_, viewEndFrame_, notes, MAX_NOTES_BUFFER);
    count = std::min(count, MAX_NOTES_BUFFER);

    bool onEdge = false;
    int hitIdx = hitTestNote(pos, notes, count, onEdge);

    if (hitIdx >= 0) {
        const auto& hitNote = notes[hitIdx];
        
        // Single selection behavior unless Shift is held
        if (!(event->modifiers() & Qt::ShiftModifier)) {
            selectedNoteIds_.clear();
        }
        selectedNoteIds_.push_back(hitNote.noteId);
        emit selectionChanged();

        if (activeTool_ == PianoRollTool::Erase) {
            controller_->removeNote(hitNote.noteId);
            selectedNoteIds_.clear();
            emit selectionChanged();
            emit notesMutated();
            update();
            return;
        }

        // Setup drag action
        activeDragNoteId_ = hitNote.noteId;
        dragStartFrame_ = xToFrame(pos.x());
        dragStartPitch_ = yToPitch(pos.y());
        dragOrigStartFrame_ = hitNote.startSample;
        dragOrigDuration_ = hitNote.endSample - hitNote.startSample;
        dragOrigPitch_ = hitNote.pitch;

        if (onEdge) {
            dragState_ = DragState::ResizingNote;
            setCursor(Qt::SizeHorCursor);
        } else {
            dragState_ = DragState::MovingNote;
            setCursor(Qt::SizeAllCursor);
        }

        // Instantly preview notes on left click selection
        controller_->previewNote(hitNote.pitch, hitNote.velocity, hitNote.channel, 120);
    } else {
        if (activeTool_ == PianoRollTool::Draw) {
            uint64_t snappedStart = snapFrame(xToFrame(pos.x()));
            // Draw note: 16th note length by default
            uint64_t noteLen = 44100 / 4; // ~16th note at 120 bpm
            if (timeline_) {
                uint32_t tpb = timeline_->getTicksPerBeat();
                uint64_t startTicks = timeline_->samplesToTicks(snappedStart);
                uint64_t endTicks = startTicks + (tpb / 4); // 1/4 of a beat (16th note)
                uint64_t endSample = timeline_->ticksToSamples(endTicks);
                if (endSample > snappedStart) {
                    noteLen = endSample - snappedStart;
                }
            }
            uint8_t pitch = yToPitch(pos.y());

            // Preview note instantly on draw
            controller_->previewNote(pitch, 100, 0, 150);

            bridge::NoteID newId = controller_->addNote(pitch, 100, 0, snappedStart, snappedStart + noteLen);
            if (newId.isValid()) {
                selectedNoteIds_.clear();
                selectedNoteIds_.push_back(newId);
                emit selectionChanged();
                emit notesMutated();
            }
            dragState_ = DragState::DrawingNote;
        } else {
            // Selection rubber band
            dragState_ = DragState::SelectRubberBand;
            rubberBandRect_ = QRectF(pos, pos);
            rubberBandVisible_ = true;
        }
    }
    update();
}

void PianoRollCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (!controller_) return;

    QPointF pos = event->position();

    // If sliding/dragging on the piano keys, play next note and release previous
    if (isPianoKeyActive_) {
        uint8_t newPitch = yToPitch(pos.y());
        if (newPitch != activeClickedPitch_) {
            controller_->noteOff(activeClickedPitch_, 0);
            activeClickedPitch_ = newPitch;
            controller_->noteOn(newPitch, 100, 0);
            update();
        }
        return;
    }

    // Hover cursor feedback
    if (dragState_ == DragState::Idle) {
        composition::MIDINote notes[MAX_NOTES_BUFFER];
        uint32_t count = controller_->getNotesInViewport(viewStartFrame_, viewEndFrame_, notes, MAX_NOTES_BUFFER);
        count = std::min(count, MAX_NOTES_BUFFER);

        bool onEdge = false;
        int hitIdx = hitTestNote(pos, notes, count, onEdge);
        if (hitIdx >= 0) {
            setCursor(onEdge ? Qt::SizeHorCursor : Qt::PointingHandCursor);
        } else {
            setCursor(activeTool_ == PianoRollTool::Draw ? Qt::CrossCursor : Qt::ArrowCursor);
        }
        return;
    }

    if (dragState_ == DragState::MovingNote) {
        uint64_t curFrame = xToFrame(pos.x());
        uint8_t curPitch = yToPitch(pos.y());

        int64_t frameDelta = static_cast<int64_t>(curFrame) - static_cast<int64_t>(dragStartFrame_);
        int8_t pitchDelta = static_cast<int8_t>(curPitch) - static_cast<int8_t>(dragStartPitch_);

        uint64_t newStart = static_cast<uint64_t>(std::clamp(
            static_cast<int64_t>(dragOrigStartFrame_) + frameDelta,
            int64_t{0},
            static_cast<int64_t>(UINT64_MAX)
        ));

        uint8_t newPitch = static_cast<uint8_t>(std::clamp(
            static_cast<int32_t>(dragOrigPitch_) + pitchDelta,
            0, 127
        ));

        controller_->moveNote(activeDragNoteId_, newPitch, snapFrame(newStart));
        emit notesMutated();
    } else if (dragState_ == DragState::ResizingNote) {
        uint64_t curFrame = xToFrame(pos.x());
        int64_t frameDelta = static_cast<int64_t>(curFrame) - static_cast<int64_t>(dragStartFrame_);

        uint64_t minDuration = 44100 / 16;
        if (timeline_) {
            uint32_t tpb = timeline_->getTicksPerBeat();
            uint64_t startTicks = timeline_->samplesToTicks(dragOrigStartFrame_);
            uint64_t endTicks = startTicks + (tpb / 16); // 1/16 of a beat (64th note)
            uint64_t endSample = timeline_->ticksToSamples(endTicks);
            if (endSample > dragOrigStartFrame_) {
                minDuration = endSample - dragOrigStartFrame_;
            }
        }

        uint64_t newDuration = static_cast<uint64_t>(std::max(
            static_cast<int64_t>(minDuration),
            static_cast<int64_t>(dragOrigDuration_) + frameDelta
        ));

        controller_->resizeNote(activeDragNoteId_, snapFrame(dragOrigStartFrame_ + newDuration));
        emit notesMutated();
    } else if (dragState_ == DragState::SelectRubberBand) {
        rubberBandRect_ = QRectF(dragAnchorPos_, pos).normalized();
        
        // Select notes inside the rubber band
        composition::MIDINote notes[MAX_NOTES_BUFFER];
        uint32_t count = controller_->getNotesInViewport(viewStartFrame_, viewEndFrame_, notes, MAX_NOTES_BUFFER);
        count = std::min(count, MAX_NOTES_BUFFER);

        selectedNoteIds_.clear();
        for (uint32_t i = 0; i < count; ++i) {
            QRectF r = noteToRect(notes[i]);
            if (rubberBandRect_.intersects(r)) {
                selectedNoteIds_.push_back(notes[i].noteId);
            }
        }
        emit selectionChanged();
    }

    update();
}

void PianoRollCanvas::mouseReleaseEvent(QMouseEvent* /*event*/) {
    if (isPianoKeyActive_) {
        controller_->noteOff(activeClickedPitch_, 0);
        isPianoKeyActive_ = false;
        activeClickedPitch_ = 0;
        update();
        return;
    }

    dragState_ = DragState::Idle;
    rubberBandVisible_ = false;
    setCursor(Qt::ArrowCursor);
    update();
}

void PianoRollCanvas::wheelEvent(QWheelEvent* event) {
    const double deltaY = event->angleDelta().y();
    const double deltaX = event->angleDelta().x();

    if (event->modifiers() & Qt::AltModifier) {
        if (deltaY != 0.0) {
            int delta = (deltaY > 0.0) ? 5 : -5;
            adjustVelocity(delta);
        }
        return;
    }

    if (event->modifiers() & Qt::ControlModifier) {
        // Vertical pitch zoom
        double deltaVal = (deltaY > 0.0) ? 2.0 : -2.0;
        noteHeight_ = std::clamp(noteHeight_ + deltaVal, 6.0, 48.0);
        
        // Center zoom adjust min/max pitch
        int32_t pitchCount = static_cast<int32_t>(height() / noteHeight_);
        pitchCount = std::clamp(pitchCount, 5, 127);
        
        uint8_t midPitch = minPitch_ + (maxPitch_ - minPitch_) / 2;
        uint8_t newMin = static_cast<uint8_t>(std::clamp(midPitch - pitchCount / 2, 0, 127));
        uint8_t newMax = static_cast<uint8_t>(std::clamp(midPitch + pitchCount / 2, 0, 127));
        
        if (minPitch_ != newMin || maxPitch_ != newMax) {
            minPitch_ = newMin;
            maxPitch_ = newMax;
            update();
            emit pitchRangeChanged(minPitch_, maxPitch_);
        }
    } else {
        bool isHorizontal = (std::abs(deltaX) > std::abs(deltaY)) || (event->modifiers() & Qt::ShiftModifier);
        
        if (isHorizontal) {
            // Horizontal scroll (timeline)
            double hDelta = (event->modifiers() & Qt::ShiftModifier) ? deltaY : deltaX;
            int64_t scrollAmt = static_cast<int64_t>(hDelta * 100.0);
            int64_t newStart = static_cast<int64_t>(viewStartFrame_) - scrollAmt;
            int64_t newEnd = static_cast<int64_t>(viewEndFrame_) - scrollAmt;

            if (newStart >= 0) {
                viewStartFrame_ = static_cast<uint64_t>(newStart);
                viewEndFrame_ = static_cast<uint64_t>(newEnd);
                update();
                emit viewportRangeChanged(viewStartFrame_, viewEndFrame_);
            }
        } else {
            // Vertical scroll (pitch shift up/down)
            if ((deltaY > 0.0 && verticalScrollAccumulator_ < 0.0) || 
                (deltaY < 0.0 && verticalScrollAccumulator_ > 0.0)) {
                verticalScrollAccumulator_ = 0.0;
            }
            verticalScrollAccumulator_ += deltaY;

            int32_t semitones = static_cast<int32_t>(verticalScrollAccumulator_ / VERTICAL_SCROLL_THRESHOLD);
            if (semitones != 0) {
                verticalScrollAccumulator_ -= semitones * VERTICAL_SCROLL_THRESHOLD;

                int32_t currentCount = static_cast<int32_t>(maxPitch_ - minPitch_ + 1);
                int32_t newMin = static_cast<int32_t>(minPitch_) + semitones;
                int32_t newMax = static_cast<int32_t>(maxPitch_) + semitones;

                if (newMin < 0) {
                    newMin = 0;
                    newMax = currentCount - 1;
                }
                if (newMax > 127) {
                    newMax = 127;
                    newMin = 127 - (currentCount - 1);
                }

                if (newMin >= 0 && newMax <= 127 && (minPitch_ != newMin || maxPitch_ != newMax)) {
                    minPitch_ = static_cast<uint8_t>(newMin);
                    maxPitch_ = static_cast<uint8_t>(newMax);
                    update();
                    emit pitchRangeChanged(minPitch_, maxPitch_);
                }
            }
        }
    }
    event->accept();
}

void PianoRollCanvas::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
}

void PianoRollCanvas::splitNotesAtPlayhead(uint64_t playheadFrame) {
    if (!controller_ || selectedNoteIds_.empty()) return;
    composition::MIDINote notes[MAX_NOTES_BUFFER];
    uint32_t count = controller_->getNotesInViewport(viewStartFrame_, viewEndFrame_, notes, MAX_NOTES_BUFFER);
    std::unordered_set<uint64_t> selSet;
    for (const auto& id : selectedNoteIds_) selSet.insert(id.toRaw());

    for (uint32_t i = 0; i < count; ++i) {
        if (selSet.find(notes[i].noteId.toRaw()) != selSet.end()) {
            if (playheadFrame > notes[i].startSample && playheadFrame < notes[i].endSample) {
                uint64_t origEnd = notes[i].endSample;
                controller_->resizeNote(notes[i].noteId, playheadFrame);
                controller_->addNote(notes[i].pitch, notes[i].velocity, notes[i].channel, playheadFrame, origEnd);
            }
        }
    }
    emit notesMutated();
    update();
}

void PianoRollCanvas::duplicateSelection() {
    if (!controller_ || selectedNoteIds_.empty()) return;
    composition::MIDINote notes[MAX_NOTES_BUFFER];
    uint32_t count = controller_->getNotesInViewport(viewStartFrame_, viewEndFrame_, notes, MAX_NOTES_BUFFER);
    std::unordered_set<uint64_t> selSet;
    for (const auto& id : selectedNoteIds_) selSet.insert(id.toRaw());

    std::vector<bridge::NoteID> newSelected;
    for (uint32_t i = 0; i < count; ++i) {
        if (selSet.find(notes[i].noteId.toRaw()) != selSet.end()) {
            uint64_t duration = (notes[i].endSample > notes[i].startSample) ? (notes[i].endSample - notes[i].startSample) : notes[i].durationSample;
            uint64_t newStart = notes[i].endSample;
            uint64_t newEnd = newStart + duration;
            bridge::NoteID newId = controller_->addNote(notes[i].pitch, notes[i].velocity, notes[i].channel, newStart, newEnd);
            if (newId.isValid()) {
                newSelected.push_back(newId);
            }
        }
    }
    if (!newSelected.empty()) {
        selectedNoteIds_ = std::move(newSelected);
        emit selectionChanged();
    }
    emit notesMutated();
    update();
}

void PianoRollCanvas::toggleMuteSelection() {
    if (!controller_ || selectedNoteIds_.empty()) return;
    composition::MIDINote notes[MAX_NOTES_BUFFER];
    uint32_t count = controller_->getNotesInViewport(viewStartFrame_, viewEndFrame_, notes, MAX_NOTES_BUFFER);
    std::unordered_set<uint64_t> selSet;
    for (const auto& id : selectedNoteIds_) selSet.insert(id.toRaw());

    for (uint32_t i = 0; i < count; ++i) {
        if (selSet.find(notes[i].noteId.toRaw()) != selSet.end()) {
            uint8_t newVel = (notes[i].velocity == 0) ? 100 : 0;
            controller_->setNoteVelocity(notes[i].noteId, newVel);
        }
    }
    emit notesMutated();
    update();
}

void PianoRollCanvas::glueSelection() {
    if (!controller_ || selectedNoteIds_.empty()) return;
    composition::MIDINote notes[MAX_NOTES_BUFFER];
    uint32_t count = controller_->getNotesInViewport(viewStartFrame_, viewEndFrame_, notes, MAX_NOTES_BUFFER);
    std::unordered_set<uint64_t> selSet;
    for (const auto& id : selectedNoteIds_) selSet.insert(id.toRaw());

    std::unordered_map<uint8_t, std::vector<composition::MIDINote>> notesByPitch;
    for (uint32_t i = 0; i < count; ++i) {
        if (selSet.find(notes[i].noteId.toRaw()) != selSet.end()) {
            notesByPitch[notes[i].pitch].push_back(notes[i]);
        }
    }

    std::vector<bridge::NoteID> gluedIds;
    for (auto& [pitch, pitchNotes] : notesByPitch) {
        if (pitchNotes.size() < 2) {
            for (const auto& n : pitchNotes) gluedIds.push_back(n.noteId);
            continue;
        }
        uint64_t minStart = UINT64_MAX;
        uint64_t maxEnd = 0;
        uint8_t vel = pitchNotes[0].velocity;
        uint8_t ch = pitchNotes[0].channel;
        for (const auto& n : pitchNotes) {
            minStart = std::min(minStart, n.startSample);
            maxEnd = std::max(maxEnd, n.endSample);
            controller_->removeNote(n.noteId);
        }
        bridge::NoteID newId = controller_->addNote(pitch, vel, ch, minStart, maxEnd);
        if (newId.isValid()) gluedIds.push_back(newId);
    }
    selectedNoteIds_ = std::move(gluedIds);
    emit selectionChanged();
    emit notesMutated();
    update();
}

void PianoRollCanvas::forceLegatoSelection() {
    if (!controller_ || selectedNoteIds_.empty()) return;
    composition::MIDINote notes[MAX_NOTES_BUFFER];
    uint32_t count = controller_->getNotesInViewport(viewStartFrame_, viewEndFrame_, notes, MAX_NOTES_BUFFER);
    std::unordered_set<uint64_t> selSet;
    for (const auto& id : selectedNoteIds_) selSet.insert(id.toRaw());

    std::vector<composition::MIDINote> selNotes;
    for (uint32_t i = 0; i < count; ++i) {
        if (selSet.find(notes[i].noteId.toRaw()) != selSet.end()) {
            selNotes.push_back(notes[i]);
        }
    }
    std::sort(selNotes.begin(), selNotes.end(), [](const auto& a, const auto& b) {
        return a.startSample < b.startSample;
    });

    for (size_t i = 0; i + 1 < selNotes.size(); ++i) {
        if (selNotes[i + 1].startSample > selNotes[i].startSample) {
            controller_->resizeNote(selNotes[i].noteId, selNotes[i + 1].startSample);
        }
    }
    emit notesMutated();
    update();
}

void PianoRollCanvas::invertSelection() {
    if (!controller_) return;
    composition::MIDINote notes[MAX_NOTES_BUFFER];
    uint32_t count = controller_->getNotesInViewport(viewStartFrame_, viewEndFrame_, notes, MAX_NOTES_BUFFER);
    std::unordered_set<uint64_t> currentSel;
    for (const auto& id : selectedNoteIds_) currentSel.insert(id.toRaw());

    std::vector<bridge::NoteID> newSel;
    for (uint32_t i = 0; i < count; ++i) {
        if (currentSel.find(notes[i].noteId.toRaw()) == currentSel.end()) {
            newSel.push_back(notes[i].noteId);
        }
    }
    selectedNoteIds_ = std::move(newSel);
    emit selectionChanged();
    update();
}

void PianoRollCanvas::adjustVelocity(int delta) {
    if (!controller_ || selectedNoteIds_.empty()) return;
    composition::MIDINote notes[MAX_NOTES_BUFFER];
    uint32_t count = controller_->getNotesInViewport(viewStartFrame_, viewEndFrame_, notes, MAX_NOTES_BUFFER);
    std::unordered_set<uint64_t> selSet;
    for (const auto& id : selectedNoteIds_) selSet.insert(id.toRaw());

    for (uint32_t i = 0; i < count; ++i) {
        if (selSet.find(notes[i].noteId.toRaw()) != selSet.end()) {
            int newVel = std::clamp(static_cast<int>(notes[i].velocity) + delta, 1, 127);
            controller_->setNoteVelocity(notes[i].noteId, static_cast<uint8_t>(newVel));
        }
    }
    emit notesMutated();
    update();
}

} // namespace presentation::views
