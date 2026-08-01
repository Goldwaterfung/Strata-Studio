// src/Middle Bridge/iinput_mode_controller.h
#pragma once

#include <cstdint>

namespace bridge {

enum class SnapMode : uint8_t {
    Free,
    Bar,
    Note_1_2,
    Note_1_4,
    Note_1_8,
    Note_1_16,
    Note_1_32,
    Note_1_64,
    Note_1_2_Triplet,
    Note_1_4_Triplet,
    Note_1_8_Triplet,
    Note_1_16_Triplet,
    Note_1_32_Triplet,
    Note_1_64_Triplet
};

enum class LoopRecordMode : uint8_t {
    Overwrite,  // New take overwrites existing
    Layer       // New take creates layers (stacked)
};

/**
 * @brief Controller interface for input mode settings.
 *
 * Manages global recording and composition input modifiers:
 * typing keyboard mapping, snap/quantization grid, loop recording
 * behavior, and automation link (MIDI learn) activation.
 */
class IInputModeController {
public:
    virtual ~IInputModeController() = default;

    // --- Typing Keyboard → Piano ---
    virtual void setTypingKeyboardToPiano(bool enabled) = 0;
    virtual bool isTypingKeyboardToPiano() const = 0;

    // --- Snap / Grid ---
    virtual void setSnapMode(SnapMode mode) = 0;
    virtual SnapMode getSnapMode() const = 0;

    // --- Loop Recording Mode ---
    virtual void setLoopRecordMode(LoopRecordMode mode) = 0;
    virtual LoopRecordMode getLoopRecordMode() const = 0;

    // --- Automation Link (MIDI Learn) ---
    virtual void setAutomationLinkActive(bool active) = 0;
    virtual bool isAutomationLinkActive() const = 0;
};

} // namespace bridge
