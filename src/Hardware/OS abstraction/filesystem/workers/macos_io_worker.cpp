// macos_io_worker.cpp
// Layer 1: Hardware/OS Abstraction - macOS IO Worker Implementation

#include "../ifile_system.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/resource.h>

namespace Layer1 {

class MacOSIOWorker {
public:
    static void setIOPriority(int fd, IOPriority priority) {
        if (fd == -1) return;

        // macOS specific IO hints
        switch (priority) {
            case IOPriority::REALTIME:
                // For real-time, we disable caching to prevent stalls and set highest priority
                fcntl(fd, F_NOCACHE, 1);
                setiopolicy_np(IOPOL_TYPE_DISK, IOPOL_SCOPE_THREAD, IOPOL_IMPORTANT);
                break;
            case IOPriority::HIGH:
                fcntl(fd, F_NOCACHE, 1);
                setiopolicy_np(IOPOL_TYPE_DISK, IOPOL_SCOPE_THREAD, IOPOL_STANDARD);
                break;
            case IOPriority::NORMAL:
                fcntl(fd, F_NOCACHE, 0);
                setiopolicy_np(IOPOL_TYPE_DISK, IOPOL_SCOPE_THREAD, IOPOL_STANDARD);
                break;
            case IOPriority::LOW:
                // Background IO
                setiopolicy_np(IOPOL_TYPE_DISK, IOPOL_SCOPE_THREAD, IOPOL_THROTTLE);
                break;
        }
    }
};

} // namespace Layer1
