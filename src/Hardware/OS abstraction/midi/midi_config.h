// midi_config.h
// Layer 1: Hardware/OS Abstraction - Unified MIDI Parameters
// This file centralizes all hardcoded parameters for MIDI drivers.

#pragma once

#include <cstdint>
#include <project_config.h>

namespace Layer1 {

/**
 * @brief Maximum number of real-time MIDI ports that can be active simultaneously
 * for the popMIDIEvent polling mechanism.
 */
static constexpr uint32_t MIDI_MAX_RT_PORTS = 16;

/**
 * @brief Default capacity for MIDI input queues if not specified.
 */
static constexpr uint32_t MIDI_DEFAULT_QUEUE_CAPACITY = 256;

/**
 * @brief Default name for virtual MIDI input ports.
 */
static const char* const MIDI_DEFAULT_VIRTUAL_PORT_NAME = "Virtual Input";

/**
 * @brief Default client name for MIDI APIs that require it (JACK, CoreMIDI).
 */
static const char* const MIDI_CLIENT_NAME = config::PROJECT_NAME.data();

/**
 * @brief Default input port name prefix for MIDI APIs (CoreMIDI, JACK).
 */
static const char* const MIDI_INPUT_PORT_NAME_PREFIX = "Input";

} // namespace Layer1
