// thread_manager_base.h
// Layer 1: Hardware/OS Abstraction - Common Thread Manager Base
// Handles handle management, watchdog logic, and common thread lifecycle

#pragma once

#include "ithread_manager.h"
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <map>
#include <condition_variable>
#include <string>

namespace Layer1 {

class ThreadManagerBase : public IThreadManager {
public:
    ThreadManagerBase();
    virtual ~ThreadManagerBase();

    // IThreadManager Implementation
    bool setRealTimeConstraints(ThreadHandle handle, const RealTimeConstraints& constraints) override;
    WatchdogHandle startWatchdog(ThreadHandle handle, uint64_t timeoutNs, WatchdogCallback callback) override;
    void stopWatchdog(WatchdogHandle handle) override;
    void pulseWatchdog(WatchdogHandle handle) override;
    void joinThread(ThreadHandle handle) override;

protected:
    struct ThreadEntry {
        ThreadHandle handle;
        std::thread thread;
        std::unique_ptr<void, void(*)(void*)> contextHolder{nullptr, [](void*){}};
        RealTimeConstraints constraints;
        bool active = false;
        std::atomic<uint64_t> lastPulseNs{0};
    };

    struct WatchdogEntry {
        WatchdogHandle handle;
        ThreadHandle threadHandle;
        uint64_t timeoutNs;
        WatchdogCallback callback;
        bool active = false;
    };

    // Implementation of createThreadImpl that handles common logic
    ThreadHandle createThreadImpl(ThreadFunction func,
                                void* context,
                                std::unique_ptr<void, void(*)(void*)> contextHolder,
                                const char* name,
                                ThreadPriority priority,
                                const RealTimeConstraints& rt,
                                uint32_t stackSize,
                                uint32_t core) override;

    // Reusable shutdown logic
    void shutdownThreads();

    // Platform-specific hooks
    virtual void applyThreadName(std::thread& thread, const char* name) = 0;
    virtual void applyThreadPriority(std::thread& thread, ThreadPriority priority, const RealTimeConstraints& rt) = 0;
    virtual void applyThreadAffinity(std::thread& thread, uint32_t core) = 0;
    virtual bool applyRealTimeConstraints(ThreadHandle handle, const RealTimeConstraints& constraints) = 0;

    // Internal helpers
    bool isValidHandleInternal(ThreadHandle handle) const;
    
    std::vector<std::unique_ptr<ThreadEntry>> threads;
    std::vector<uint32_t> freeSlots;
    mutable std::mutex threadsMutex;

    // Watchdog management
    std::map<uint32_t, WatchdogEntry> watchdogs;
    std::mutex watchdogMutex;
    uint32_t nextWatchdogId = 1;
    std::thread watchdogThread;
    std::atomic<bool> watchdogShutdown{false};
    std::condition_variable watchdogCV;

    void watchdogWorker();
};

} // namespace Layer1
