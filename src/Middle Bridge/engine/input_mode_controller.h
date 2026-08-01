// src/Middle Bridge/input_mode_controller.h
#pragma once

#include "engine/iinput_mode_controller.h"

namespace bridge {

/**
 * @brief Concrete implementation of IInputModeController.
 *
 * Pure state storage with no lower-layer dependencies.
 * All state is owned by the Presentation layer through this bridge controller.
 */
class InputModeController : public IInputModeController {
public:
    InputModeController() = default;
    ~InputModeController() override = default;

    // --- Typing Keyboard → Piano ---
    void setTypingKeyboardToPiano(bool enabled) override;
    bool isTypingKeyboardToPiano() const override;

    // --- Snap / Grid ---
    void setSnapMode(SnapMode mode) override;
    SnapMode getSnapMode() const override;

    // --- Loop Recording Mode ---
    void setLoopRecordMode(LoopRecordMode mode) override;
    LoopRecordMode getLoopRecordMode() const override;

    // --- Automation Link (MIDI Learn) ---
    void setAutomationLinkActive(bool active) override;
    bool isAutomationLinkActive() const override;

private:
    bool typingKeyboardToPiano_ = false;
    SnapMode snapMode_ = SnapMode::Note_1_4;
    LoopRecordMode loopRecordMode_ = LoopRecordMode::Overwrite;
    bool automationLinkActive_ = false;
};

} // namespace bridge
