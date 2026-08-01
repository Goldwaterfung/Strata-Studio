// src/Presentation/views/playlist/clips/AutomationClipItem.h
#pragma once

#include <QPainter>
#include <QRectF>

#include "automation/iautomation_controller.h"
#include "timeline/iarrangement_controller.h"

namespace presentation::views {

/**
 * @brief Stateless, all-static paint helper for Automation clip thumbnails.
 *
 * Renders an automation curve as a QPainterPath cubic Bézier stroked in
 * AccentGlow (#A78BFA). Control points are fetched from IAutomationController
 * via a stack-allocated VisualAutomationPoint[MAX_POINTS] buffer.
 *
 * Zero heap allocation — QPainterPath is stack-scoped, curve built and
 * stroked within the single paint() call.
 */
class AutomationClipItem {
public:
    AutomationClipItem() = delete;

    static constexpr uint32_t MAX_POINTS = 2048;

    static void drawCurve(QPainter& p, const QRectF& innerRect,
                          const bridge::VisualAutomationPoint* pts,
                          uint32_t count,
                          uint64_t regionStart, uint64_t regionDuration,
                          bool editable = true,
                          double defaultNormalizedValue = 0.5);

    static void drawControlPoints(QPainter& p, const QRectF& innerRect,
                                  const bridge::VisualAutomationPoint* pts,
                                  uint32_t count,
                                  uint64_t regionStart, uint64_t regionDuration);

    static bool hitTestCurve(const QPointF& cursorPosWidget,
                             const QRectF& innerRect,
                             const bridge::VisualAutomationPoint* pts,
                             uint32_t count,
                             uint64_t regionStart, uint64_t regionDuration,
                             float* outNormalizedValue,
                             double defaultNormalizedValue = 0.5);
};

} // namespace presentation::views
