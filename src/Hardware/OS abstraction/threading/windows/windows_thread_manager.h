// windows_thread_manager.h
// Layer 1: Hardware/OS Abstraction - Windows Thread Manager Implementation

#pragma once

#include "../thread_manager_base.h"

#ifdef _WIN32

namespace Layer1 {

class WindowsThreadManager : public ThreadManagerBase {
public:
    WindowsThreadManager() = default;
    ~WindowsThreadManager() override = default;

protected:
    // Platform-specific hooks
    void applyThreadName(std::thread& thread, const char* name) override;
    void applyThreadPriority(std::thread& thread, ThreadPriority priority, const RealTimeConstraints& rt) override;
    void applyThreadAffinity(std::thread& thread, uint32_t core) override;
    bool applyRealTimeConstraints(ThreadHandle handle, const RealTimeConstraints& constraints) override;

    // MMCSS implementation
    bool joinWorkgroup(WorkgroupHandle handle) override;
    void leaveWorkgroup() override;
};

} // namespace Layer1

#endif // _WIN32
