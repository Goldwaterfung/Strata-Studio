// src/Presentation/views/base_tactile_control.cpp
#include "base_tactile_control.h"
#include <QGuiApplication>
#include <algorithm>

namespace presentation::views {

BaseTactileControl::BaseTactileControl(QWidget* parent)
    : QWidget(parent) {
    // Enable strong focus so the widget can be tabbed to and adjusted via keyboard
    setFocusPolicy(Qt::StrongFocus);
}

void BaseTactileControl::setValue(float val) {
    // Clamp the parameter value to strict normalized boundaries [0.0f, 1.0f]
    float clamped = std::clamp(val, 0.0f, 1.0f);
    
    if (clamped != m_value) {
        m_value = clamped;
        Q_EMIT valueChanged(m_value);
        update(); // Schedule repaint with the new visual state
    }
}

void BaseTactileControl::setDefaultValue(float val) {
    m_defaultValue = std::clamp(val, 0.0f, 1.0f);
}

void BaseTactileControl::resetToDefault() {
    setValue(m_defaultValue);
}

void BaseTactileControl::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // Option/Alt + Left Click triggers an instant default reset (standard DAW behavior)
        if (event->modifiers() & Qt::AltModifier) {
            resetToDefault();
            event->accept();
            return;
        }

        m_isDragging = true;
        m_lastMousePos = event->position();
        
        // Hide cursor to enable infinite physical dragging without running into screen boundaries
        setCursor(Qt::BlankCursor);
        Q_EMIT controlPressed();
        event->accept();
    } else {
        QWidget::mousePressEvent(event);
    }
}

void BaseTactileControl::mouseMoveEvent(QMouseEvent* event) {
    if (m_isDragging) {
        // Calculate vertical drag offset: dragging UP increases value, DOWN decreases value
        double deltaY = m_lastMousePos.y() - event->position().y();
        
        float currentSensitivity = m_sensitivity;
        
        // Hold Shift for high-precision fine adjustment (divides delta sensitivity by 10)
        if (event->modifiers() & Qt::ShiftModifier) {
            currentSensitivity *= 0.1f;
        }

        float deltaVal = static_cast<float>(deltaY) * currentSensitivity;
        setValue(m_value + deltaVal);

        m_lastMousePos = event->position();
        event->accept();
    } else {
        QWidget::mouseMoveEvent(event);
    }
}

void BaseTactileControl::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_isDragging) {
        m_isDragging = false;
        
        // Restore standard arrow cursor
        setCursor(Qt::ArrowCursor);
        Q_EMIT controlReleased();
        event->accept();
    } else {
        QWidget::mouseReleaseEvent(event);
    }
}

void BaseTactileControl::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // Double-click instantly resets parameter to default state
        resetToDefault();
        event->accept();
    } else {
        QWidget::mouseDoubleClickEvent(event);
    }
}

void BaseTactileControl::wheelEvent(QWheelEvent* event) {
    // Direct scroll wheel adjustment when focused or hovered
    int angleDelta = event->angleDelta().y();
    
    // 120 angle delta units represents standard one-notch scroll wheel step
    float scrollSteps = static_cast<float>(angleDelta) / 120.0f;
    float baseStep = 0.01f; // Standard 1% step per notch
    
    if (event->modifiers() & Qt::ShiftModifier) {
        baseStep *= 0.1f; // 0.1% precise step per notch if Shift is held
    }

    setValue(m_value + (scrollSteps * baseStep));
    event->accept();
}

} // namespace presentation::views
