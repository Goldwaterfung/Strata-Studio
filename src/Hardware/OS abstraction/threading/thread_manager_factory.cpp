// thread_manager_factory.cpp
// Layer 1: Hardware/OS Abstraction - Thread Manager Factory

#include "ithread_manager.h"

#if defined(__APPLE__)
#include "macos/macos_thread_manager.h"
#elif defined(_WIN32)
#include "windows/windows_thread_manager.h"
#endif

namespace Layer1 {

std::unique_ptr<IThreadManager> IThreadManager::create() {
#if defined(__APPLE__)
    return std::make_unique<MacOSThreadManager>();
#elif defined(_WIN32)
    return std::make_unique<WindowsThreadManager>();
#else
#error "Unsupported OS platform for IThreadManager creation"
#endif
}

} // namespace Layer1
