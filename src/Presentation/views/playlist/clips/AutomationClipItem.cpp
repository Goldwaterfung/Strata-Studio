#include "AutomationClipItem.h"

#include <QPainterPath>
#include <QLinearGradient>
#include <algorithm>
#include <cmath>

#include "theme.h"
#include "common/dsp/curve_interpolation.h"

namespace presentation::views {

// ─────────────────────────────────────────────────────────────────────────────
// Public entry point
// ─────────────────────────────────────────────────────────────────────────────

void AutomationClipItem::drawCurve(
    QPainter& p,
    const QRectF& innerRect,
    const bridge::VisualAutomationPoint* pts,
    uint32_t count,
    uint64_t regionStart,
    uint64_t regionDuration,
    bool editable,
    double defaultNormalizedValue)
{
    if (regionDuration == 0) {
        return;
    }

    const double scaleX = innerRect.width()  / static_cast<double>(regionDuration);
    const double scaleY = innerRect.height();

    // Map a single automation point to widget-space coordinates, allowing off-screen coordinates
    auto ptToWidget = [&](const bridge::VisualAutomationPoint& pt) -> QPointF {
        const int64_t relFrame = static_cast<int64_t>(pt.framePosition)
                               - static_cast<int64_t>(regionStart);
        const double x = innerRect.left() + static_cast<double>(relFrame) * scaleX;
        // normalizedValue: 0.0 = bottom, 1.0 = top
        const double y = innerRect.bottom()
            - static_cast<double>(pt.normalizedValue) * scaleY;
        return QPointF(x, y);
    };

    // Build cubic Bézier path
    QPainterPath curvePath;

    if (count == 0) {
        // If no points exist, draw a flat line at the default normalized value
        const double y = innerRect.bottom() - defaultNormalizedValue * scaleY;
        curvePath.moveTo(QPointF(innerRect.left(), y));
        curvePath.lineTo(QPointF(innerRect.right(), y));
    } else {
        const QPointF pStart = ptToWidget(pts[0]);
        // If the first point is inside or to the right of the viewport start, extend from left edge
        if (pts[0].framePosition > regionStart) {
            curvePath.moveTo(QPointF(innerRect.left(), pStart.y()));
            curvePath.lineTo(pStart);
        } else {
            curvePath.moveTo(pStart);
        }

        for (uint32_t i = 1; i < count; ++i) {
            const bridge::VisualAutomationPoint& pt0 = pts[i - 1];
            const bridge::VisualAutomationPoint& pt1 = pts[i];

            const QPointF p0 = ptToWidget(pt0);
            const QPointF p1 = ptToWidget(pt1);

            const double width = p1.x() - p0.x();
            if (width <= 0.0 || pt1.framePosition <= pt0.framePosition) {
                curvePath.lineTo(p1);
                continue;
            }

            ::AutomationPoint dspP1;
            dspP1.positionSample = pt0.framePosition;
            dspP1.value = pt0.normalizedValue;
            dspP1.curveShape = static_cast<::AutomationPoint::Shape>(pt0.curveShape);
            dspP1.tension = pt0.tension;

            ::AutomationPoint dspP2;
            dspP2.positionSample = pt1.framePosition;
            dspP2.value = pt1.normalizedValue;
            dspP2.curveShape = static_cast<::AutomationPoint::Shape>(pt1.curveShape);
            dspP2.tension = pt1.tension;

            if (dspP1.curveShape == ::AutomationPoint::Shape::STEP) {
                curvePath.lineTo(QPointF(p1.x(), p0.y()));
                curvePath.lineTo(p1);
            } else if (dspP1.curveShape == ::AutomationPoint::Shape::SQUARE) {
                const double midX = p0.x() + (p1.x() - p0.x()) * 0.5;
                curvePath.lineTo(QPointF(midX, p0.y()));
                curvePath.lineTo(QPointF(midX, p1.y()));
                curvePath.lineTo(p1);
            } else {
                const int steps = std::max<int>(1, static_cast<int>(std::ceil(width)));
                for (int step = 1; step <= steps; ++step) {
                    const double t = static_cast<double>(step) / steps;
                    const double curX = p0.x() + t * width;
                    
                    const uint64_t curFrame = pt0.framePosition + static_cast<uint64_t>(t * static_cast<double>(pt1.framePosition - pt0.framePosition));
                    
                    const float interpNormalizedValue = DSP::CurveInterpolator::calculate(dspP1, dspP2, curFrame);
                    
                    const double yPx = innerRect.bottom() - static_cast<double>(interpNormalizedValue) * scaleY;
                    
                    curvePath.lineTo(QPointF(curX, yPx));
                }
            }
        }

        const QPointF pEnd = ptToWidget(pts[count - 1]);
        // If the last point is to the left of the viewport end, extend to the right edge
        if (pts[count - 1].framePosition < regionStart + regionDuration) {
            curvePath.lineTo(QPointF(innerRect.right(), pEnd.y()));
        }
    }

    if (editable) {
        // Filled area under the curve (subtle AccentGlow wash)
        QPainterPath fillPath = curvePath;
        fillPath.lineTo(innerRect.bottomRight());
        fillPath.lineTo(innerRect.bottomLeft());
        fillPath.closeSubpath();

        QLinearGradient fillGrad(innerRect.topLeft(), innerRect.bottomLeft());
        QColor gradStart = theme::Color::AccentGlow;
        gradStart.setAlpha(35);
        QColor gradEnd = theme::Color::AccentGlow;
        gradEnd.setAlpha(5);
        fillGrad.setColorAt(0.0, gradStart);
        fillGrad.setColorAt(1.0, gradEnd);
        p.setPen(Qt::NoPen);
        p.fillPath(fillPath, fillGrad);

        // Curve stroke — full AccentGlow (#A78BFA)
        QColor penColor = theme::Color::AccentGlow;
        penColor.setAlpha(200);
        p.setPen(QPen(penColor, 1.5, Qt::SolidLine,
                      Qt::RoundCap, Qt::RoundJoin));
    } else {
        // Read mode: dimmed violet, dashed stroke — visible but signals non-editable
        p.setOpacity(0.45);
        QColor readPenColor = theme::Color::AccentGlow;
        readPenColor.setAlpha(120);
        p.setPen(QPen(readPenColor, 1.5, Qt::DashLine,
                      Qt::RoundCap, Qt::RoundJoin));
    }

    p.setBrush(Qt::NoBrush);
    p.drawPath(curvePath);

    // Restore opacity if we changed it
    if (!editable) {
        p.setOpacity(1.0);
    }
}

void AutomationClipItem::drawControlPoints(
    QPainter& p,
    const QRectF& innerRect,
    const bridge::VisualAutomationPoint* pts,
    uint32_t count,
    uint64_t regionStart,
    uint64_t regionDuration)
{
    if (regionDuration == 0) {
        return;
    }

    const double scaleX = innerRect.width()  / static_cast<double>(regionDuration);
    const double scaleY = innerRect.height();

    p.setPen(Qt::NoPen);

    for (uint32_t i = 0; i < count; ++i) {
        const int64_t relFrame = static_cast<int64_t>(pts[i].framePosition)
                               - static_cast<int64_t>(regionStart);
        const double x = innerRect.left() + static_cast<double>(relFrame) * scaleX;
        const double y = innerRect.bottom()
            - static_cast<double>(pts[i].normalizedValue) * scaleY;

        if (pts[i].isSelected) {
            // Selected: filled AccentGlow circle with outer ring
            QColor brushColor = theme::Color::AccentGlow;
            brushColor.setAlpha(230);
            p.setBrush(brushColor);
            p.drawEllipse(QRectF(x - 4.0, y - 4.0, 8.0, 8.0));
            p.setPen(QPen(QColor(255, 255, 255, 180), 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QRectF(x - 4.5, y - 4.5, 9.0, 9.0));
            p.setPen(Qt::NoPen);
        } else {
            // Idle: small filled dot
            QColor brushColor = theme::Color::AccentGlow;
            brushColor.setAlpha(160);
            p.setBrush(brushColor);
            p.drawEllipse(QRectF(x - 2.5, y - 2.5, 5.0, 5.0));
        }
    }
}

bool AutomationClipItem::hitTestCurve(
    const QPointF& cursorPosWidget,
    const QRectF& innerRect,
    const bridge::VisualAutomationPoint* pts,
    uint32_t count,
    uint64_t regionStart,
    uint64_t regionDuration,
    float* outNormalizedValue,
    double defaultNormalizedValue)
{
    if (regionDuration == 0 || innerRect.width() <= 0.0) {
        return false;
    }

    const double relX = cursorPosWidget.x() - innerRect.left();
    const double scaleX = innerRect.width() / static_cast<double>(regionDuration);
    const int64_t relFrame = static_cast<int64_t>(std::round(relX / scaleX));
    const int64_t frame = static_cast<int64_t>(regionStart) + relFrame;
    const uint64_t curFrame = static_cast<uint64_t>(std::max(int64_t{0}, frame));

    float interpolatedValue = static_cast<float>(defaultNormalizedValue);

    if (count > 0) {
        if (curFrame <= pts[0].framePosition) {
            interpolatedValue = pts[0].normalizedValue;
        } else if (curFrame >= pts[count - 1].framePosition) {
            interpolatedValue = pts[count - 1].normalizedValue;
        } else {
            uint32_t left = 0;
            uint32_t right = count - 1;
            while (left < right) {
                uint32_t mid = left + (right - left) / 2;
                if (pts[mid].framePosition <= curFrame) {
                    left = mid + 1;
                } else {
                    right = mid;
                }
            }

            uint32_t idx0 = left - 1;
            uint32_t idx1 = left;

            const bridge::VisualAutomationPoint& pt0 = pts[idx0];
            const bridge::VisualAutomationPoint& pt1 = pts[idx1];

            if (pt1.framePosition <= pt0.framePosition) {
                interpolatedValue = pt0.normalizedValue;
            } else {
                ::AutomationPoint dspP1;
                dspP1.positionSample = pt0.framePosition;
                dspP1.value = pt0.normalizedValue;
                dspP1.curveShape = static_cast<::AutomationPoint::Shape>(pt0.curveShape);
                dspP1.tension = pt0.tension;

                ::AutomationPoint dspP2;
                dspP2.positionSample = pt1.framePosition;
                dspP2.value = pt1.normalizedValue;
                dspP2.curveShape = static_cast<::AutomationPoint::Shape>(pt1.curveShape);
                dspP2.tension = pt1.tension;

                if (dspP1.curveShape == ::AutomationPoint::Shape::STEP) {
                    interpolatedValue = pt0.normalizedValue;
                } else if (dspP1.curveShape == ::AutomationPoint::Shape::SQUARE) {
                    uint64_t midFrame = pt0.framePosition + (pt1.framePosition - pt0.framePosition) / 2;
                    interpolatedValue = (curFrame < midFrame) ? pt0.normalizedValue : pt1.normalizedValue;
                } else {
                    interpolatedValue = DSP::CurveInterpolator::calculate(dspP1, dspP2, curFrame);
                }
            }
        }
    }

    const double scaleY = innerRect.height();
    const double curveY = innerRect.bottom() - static_cast<double>(interpolatedValue) * scaleY;

    if (std::fabs(cursorPosWidget.y() - curveY) <= 6.0) {
        if (outNormalizedValue) {
            *outNormalizedValue = interpolatedValue;
        }
        return true;
    }

    return false;
}

} // namespace presentation::views
