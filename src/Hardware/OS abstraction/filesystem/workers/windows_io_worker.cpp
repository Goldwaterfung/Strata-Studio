// windows_io_worker.cpp
// Layer 1: Hardware/OS Abstraction - Windows IO Worker Implementation

#include "../ifile_system.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace Layer1 {

class WindowsIOWorker {
public:
    static void setIOPriority(HANDLE hFile, IFileSystem::IOPriority priority) {
        if (hFile == INVALID_HANDLE_VALUE) return;

        // Windows doesn't have a simple per-handle IO priority API without using NT internal APIs.
        // However, IO priority is tied to the thread priority that initiates the IO.
        // Since StdFileSystem uses dedicated worker threads, we can set the thread priority.
        
        int winPriority = THREAD_PRIORITY_NORMAL;
        switch (priority) {
            case IFileSystem::IOPriority::REALTIME: winPriority = THREAD_PRIORITY_TIME_CRITICAL; break;
            case IFileSystem::IOPriority::HIGH:     winPriority = THREAD_PRIORITY_ABOVE_NORMAL; break;
            case IFileSystem::IOPriority::NORMAL:   winPriority = THREAD_PRIORITY_NORMAL; break;
            case IFileSystem::IOPriority::LOW:      winPriority = THREAD_PRIORITY_LOWEST; break;
        }
        
        SetThreadPriority(GetCurrentThread(), winPriority);
    }
};

} // namespace Layer1
