// src/Presentation/views/pianoroll_window/ControllerLaneView.cpp
#include "ControllerLaneView.h"
#include "Presentation/views/theme.h"
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QPainterPath>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>

namespace presentation::views {

ControllerLaneView::ControllerLaneView(bridge::IMidiEditorController* controller, QWidget* parent)
    : QWidget(parent), controller_(controller) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumHeight(80);
}

void ControllerLaneView::setViewportRange(uint64_t startFrame, uint64_t endFrame) {
    if (viewStartFrame_ != startFrame || viewEndFrame_ != endFrame) {
        viewStartFrame_ = startFrame;
        viewEndFrame_ = endFrame;
        update();
    }
}

void ControllerLaneView::setControllerNumber(uint8_t controllerNumber) {
    if (controllerNumber_ != controllerNumber) {
        controllerNumber_ = controllerNumber;
        update();
    }
}

double ControllerLaneView::frameToX(uint64_t frame) const {
    double totalFrames = static_cast<double>(viewEndFrame_ - viewStartFrame_);
    if (totalFrames <= 0.0) return KEY_WIDTH;
    double canvasWidth = static_cast<double>(width()) - KEY_WIDTH;
    double ratio = static_cast<double>(frame - viewStartFrame_) / totalFrames;
    return KEY_WIDTH + ratio * canvasWidth;
}

uint64_t ControllerLaneView::xToFrame(double x) const {
    double canvasWidth = static_cast<double>(width()) - KEY_WIDTH;
    if (canvasWidth <= 0.0) return viewStartFrame_;
    double ratio = (x - KEY_WIDTH) / canvasWidth;
    double totalFrames = static_cast<double>(viewEndFrame_ - viewStartFrame_);
    return viewStartFrame_ + static_cast<uint64_t>(std::clamp(ratio, 0.0, 1.0) * totalFrames);
}

void ControllerLaneView::paintEvent(QPaintEvent* /*event*/) {
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

    // Fetch CC points
    bridge::VisualCCPoint ccPoints[MAX_POINTS_BUFFER];
    uint32_t count = controller_->getCCPointsInViewport(viewStartFrame_, viewEndFrame_, controllerNumber_, ccPoints, MAX_POINTS_BUFFER);
    count = std::min(count, MAX_POINTS_BUFFER);

    // Sort CC points by position for a clean continuous curve
    std::sort(ccPoints, ccPoints + count, [](const bridge::VisualCCPoint& a, const bridge::VisualCCPoint& b) {
        return a.framePosition < b.framePosition;
    });

    double h = static_cast<double>(height());

    // 2. Draw CC curve
    if (count > 0) {
        QPainterPath path;
        QPainterPath fillPath;

        double startX = frameToX(ccPoints[0].framePosition);
        double startY = h - (static_cast<double>(ccPoints[0].value) / 127.0) * (h - 10.0);

        path.moveTo(startX, startY);
        fillPath.moveTo(startX, h);
        fillPath.lineTo(startX, startY);

        for (uint32_t i = 1; i < count; ++i) {
            double cx = frameToX(ccPoints[i].framePosition);
            double cy = h - (static_cast<double>(ccPoints[i].value) / 127.0) * (h - 10.0);
            
            path.lineTo(cx, cy);
            fillPath.lineTo(cx, cy);
        }

        double endX = frameToX(ccPoints[count - 1].framePosition);
        fillPath.lineTo(endX, h);
        fillPath.closeSubpath();

        // volumetric gradient glow under the curve
        QLinearGradient areaGrad(QPointF(0.0, 0.0), QPointF(0.0, h));
        QColor peakGlow = theme::Color::AccentGlow;
        peakGlow.setAlpha(60);
        QColor endGlow = theme::Color::AccentGlow;
        endGlow.setAlpha(0);
        areaGrad.setColorAt(0.0, peakGlow);
        areaGrad.setColorAt(1.0, endGlow);
        
        p.fillPath(fillPath, areaGrad);

        // outline curve in cyber mint
        p.setPen(QPen(theme::Color::AccentGlow, 2.0));
        p.drawPath(path);

        // draw points
        for (uint32_t i = 0; i < count; ++i) {
            double cx = frameToX(ccPoints[i].framePosition);
            double cy = h - (static_cast<double>(ccPoints[i].value) / 127.0) * (h - 10.0);
            
            p.setPen(QPen(QColor(255, 255, 255), 1.0));
            p.setBrush(theme::Color::AccentGlow);
            p.drawEllipse(QRectF(cx - 3.0, cy - 3.0, 6.0, 6.0));
        }
    }
}

void ControllerLaneView::mousePressEvent(QMouseEvent* event) {
    if (!controller_) return;

    isDrawing_ = true;
    controller_->beginGesture();
    QPointF pos = event->position();
    
    uint64_t frame = xToFrame(pos.x());
    double h = static_cast<double>(height());
    double valRatio = (h - pos.y()) / (h - 10.0);
    uint8_t val = static_cast<uint8_t>(std::clamp(valRatio * 127.0, 0.0, 127.0));

    // Remove existing CC points in close range to prevent overlapping clutter
    uint64_t range = 44100 / 10; // ~100ms tolerance range
    uint64_t startRange = frame > range ? frame - range : 0;
    controller_->removeCCPointsInRange(controllerNumber_, startRange, frame + range);

    controller_->addCCPoint(controllerNumber_, val, 0, frame);
    emit ccMutated();
    update();
}

void ControllerLaneView::mouseMoveEvent(QMouseEvent* event) {
    if (isDrawing_ && controller_) {
        QPointF pos = event->position();
        uint64_t frame = xToFrame(pos.x());
        double h = static_cast<double>(height());
        double valRatio = (h - pos.y()) / (h - 10.0);
        uint8_t val = static_cast<uint8_t>(std::clamp(valRatio * 127.0, 0.0, 127.0));

        uint64_t range = 44100 / 12; 
        uint64_t startRange = frame > range ? frame - range : 0;
        controller_->removeCCPointsInRange(controllerNumber_, startRange, frame + range);

        controller_->addCCPoint(controllerNumber_, val, 0, frame);
        emit ccMutated();
        update();
    }
}

void ControllerLaneView::mouseReleaseEvent(QMouseEvent* /*event*/) {
    isDrawing_ = false;
    if (controller_) {
        controller_->endGesture();
    }
}

} // namespace presentation::views
