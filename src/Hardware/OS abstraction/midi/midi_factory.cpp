#if defined(__APPLE__)
    #include "macos/coremidi_driver.h"
#elif defined(_WIN32)
    #include "windows/winmm_midi_driver.h"
#endif
#include "rtmidi_driver.h"

namespace Layer1 {

std::unique_ptr<IMIDIDriver> IMIDIDriver::create([[maybe_unused]] AudioAPI api) {
#if defined(__APPLE__)
    return std::make_unique<CoreMIDIDriver>();
#elif defined(_WIN32)
    return std::make_unique<WinMMMIDIDriver>();
#else
    return std::make_unique<RtMidiDriver>(api);
#endif
}

} // namespace Layer1
