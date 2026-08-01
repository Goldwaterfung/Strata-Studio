// macos_thread_manager.h
// Layer 1: Hardware/OS Abstraction - macOS Thread Manager Implementation

#pragma once

#include "../thread_manager_base.h"

namespace Layer1 {

class MacOSThreadManager : public ThreadManagerBase {
public:
    MacOSThreadManager() = default;
    ~MacOSThreadManager() override {
        shutdownThreads();
    }

protected:
    // Platform-specific hooks
    void applyThreadName(std::thread& thread, const char* name) override;
    void applyThreadPriority(std::thread& thread, ThreadPriority priority, const RealTimeConstraints& rt) override;
    void applyThreadAffinity(std::thread& thread, uint32_t core) override;
    bool applyRealTimeConstraints(ThreadHandle handle, const RealTimeConstraints& constraints) override;

    // Workgroup implementation
    bool joinWorkgroup(WorkgroupHandle handle) override;
    void leaveWorkgroup() override;
};

} // namespace Layer1
