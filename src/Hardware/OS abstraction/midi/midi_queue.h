// midi_queue.h
// Layer 1: Hardware/OS Abstraction - MIDI Lock-Free Queue
// Specific specialization for MIDIMessage

#pragma once

#include "../common/lock_free_queue.h"
#include "imidi_driver.h"

namespace Layer1 {

/**
 * @brief Lock-free queue specialized for MIDIMessage
 * 
 * Uses the common SPSC (Single Producer, Single Consumer) implementation
 * to pass MIDI events from the MIDI driver thread to the audio thread.
 */
using MIDIMessageQueue = ILockFreeQueue<IMIDIDriver::MIDIMessage>;

} // namespace Layer1
