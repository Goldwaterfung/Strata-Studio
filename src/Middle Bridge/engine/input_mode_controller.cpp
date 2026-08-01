// src/Middle Bridge/input_mode_controller.cpp
#include "engine/input_mode_controller.h"

namespace bridge {

void InputModeController::setTypingKeyboardToPiano(bool enabled) {
    typingKeyboardToPiano_ = enabled;
}

bool InputModeController::isTypingKeyboardToPiano() const {
    return typingKeyboardToPiano_;
}

void InputModeController::setSnapMode(SnapMode mode) {
    snapMode_ = mode;
}

SnapMode InputModeController::getSnapMode() const {
    return snapMode_;
}

void InputModeController::setLoopRecordMode(LoopRecordMode mode) {
    loopRecordMode_ = mode;
}

LoopRecordMode InputModeController::getLoopRecordMode() const {
    return loopRecordMode_;
}

void InputModeController::setAutomationLinkActive(bool active) {
    automationLinkActive_ = active;
}

bool InputModeController::isAutomationLinkActive() const {
    return automationLinkActive_;
}

} // namespace bridge
