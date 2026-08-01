// midi_spsc_ring_buffer.h
// Single Producer Single Consumer (SPSC) Ring Buffer for MIDI/Event Delivery
//
// PURPOSE:
// - Thread-safe, lock-free, wait-free ring buffer for MIDI events
// - Designed to pass events between Non-RT thread (GUI, MIDI input) and RT Audio Thread
// - Pre-allocated, zero-allocation during execution
//
// DESIGN PRINCIPLES:
// - Single Producer (Non-RT) and Single Consumer (RT)
// - Power of 2 capacity for index masking optimization
// - Cache-line aligned pointers to prevent false sharing

#pragma once

#include "common/system_primitives.h"
#include <atomic>
#include <cstring>
#include <cstddef>

namespace Layer2 {

// MIDISPSCRingBuffer has been removed as it was superseded by generic lock-free templates (ILockFreeQueue).

} // namespace Layer2

