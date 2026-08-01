// src/Presentation/views/mixer/base_slot_widget.h
#pragma once

#include <QWidget>
#include <QString>
#include <QRectF>
#include <QPainter>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include "Middle Bridge/tracks/itrack_controller.h"

namespace presentation::views {

/**
 * @brief Abstract base class representing a single insert plugin, instrument, or send slot.
 *
 * Centralizes layout size constraints, visual coordinates calculation (bypass LED circle),
 * default border and background painting, and mouse click tracking zones.
 */
class BaseSlotWidget : public QWidget {
    Q_OBJECT

public:
    explicit BaseSlotWidget(QWidget* parent = nullptr);
    virtual ~BaseSlotWidget() override = default;

    /**
     * @brief Bind this widget to a track.
     */
    void bind(bridge::ITrackController* controller, TrackID trackId);

    virtual void setAsPlusButton() = 0;
    virtual bool isEmpty() const = 0;
    virtual bool isBypassed() const = 0;
    
    // Virtual getters for concrete display details
    virtual QString displayLabel() const = 0;
    virtual QColor getLedColor() const = 0;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

    // Common click zone calculations
    QRectF bypassButtonRect() const;

    // Virtual interaction interfaces
    virtual void toggleBypass() = 0;
    virtual void openEditor() = 0;
    virtual void showRoutingMenu(const QPoint& pos) = 0;
    
    // Optional rendering hook for subclasses (e.g. sends level meter overlay)
    virtual void paintAdditional(QPainter* painter, double w, double h);

    bridge::ITrackController* m_controller = nullptr;
    TrackID                   m_trackId{};

    bool                      m_bypassed = false;
    bool                      m_isEmpty = true;
    bool                      m_isPlusButton = false;
};

} // namespace presentation::views
