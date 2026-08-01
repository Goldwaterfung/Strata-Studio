// src/Presentation/views/base_tactile_control.h
#pragma once

#include <QWidget>
#include <QPoint>
#include <QMouseEvent>
#include <QWheelEvent>

namespace presentation::views {

/**
 * @brief Abstract Base Class for custom DAW controls providing unified interaction physics.
 * Enforces the Professional DAW Interaction Suite (vertical tracking, Shift precision, Option reset).
 */
class BaseTactileControl : public QWidget {
    Q_OBJECT
    Q_PROPERTY(float value READ value WRITE setValue NOTIFY valueChanged)

public:
    explicit BaseTactileControl(QWidget* parent = nullptr);
    virtual ~BaseTactileControl() = default;

    // --- Core State Accessors ---
    float value() const { return m_value; }
    float defaultValue() const { return m_defaultValue; }
    bool isDragging() const { return m_isDragging; }
    
    /**
     * @brief Sensitivity coefficient mapping vertical pixel delta to normalized parameter delta.
     */
    float dragSensitivity() const { return m_sensitivity; }
    void setDragSensitivity(float sensitivity) { m_sensitivity = sensitivity; }

public Q_SLOTS:
    void setValue(float val);
    void setDefaultValue(float val);
    void resetToDefault();

Q_SIGNALS:
    void valueChanged(float newValue);
    void controlPressed();
    void controlReleased();

protected:
    // --- Mouse & Gesture Interaction Overrides ---
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

    // --- State variables ---
    float m_value = 0.0f;          // Normalized 0.0f to 1.0f
    float m_defaultValue = 0.5f;   // Default state for quick reset
    float m_sensitivity = 0.003f;  // Pixels to normalized value ratio

    bool m_isDragging = false;
    QPointF m_lastMousePos;
};

} // namespace presentation::views
