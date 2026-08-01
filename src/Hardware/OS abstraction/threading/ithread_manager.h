// ithread_manager.h
// Layer 1: Hardware/OS Abstraction - Thread Manager Interface
// PURE INTERFACE: No platform-specific headers allowed

#pragma once

#include <cstdint>
#include <memory>
#include <functional>
#include "../common/layer1_primitives.h"

namespace Layer1 {

// =============================================================================
// THREAD MANAGER INTERFACE
// =============================================================================

class IThreadManager {
public:
    // === Nested Types === //

    // Type-safe thread context (no void* type erasure)
    template<typename ContextType>
    class ThreadContext {
    public:
        template<typename T>
        explicit ThreadContext(T&& context)
            : contextStorage(std::make_unique<ContextType>(std::forward<T>(context))) {}
        
        ~ThreadContext() {
            // [DEBUG-context-destruct]
        }

        ContextType* get() { return contextStorage.get(); }
        const ContextType* get() const { return contextStorage.get(); }

        // Take ownership of the context
        std::unique_ptr<ContextType> release() { return std::move(contextStorage); }

    private:
        std::unique_ptr<ContextType> contextStorage;
    };

    using ThreadFunction = void(*)(void* context);
    using WatchdogCallback = void(*)(ThreadHandle);
    using WatchdogHandle = uint32_t;

    template<typename ContextType>
    struct ThreadCreateInfo {
        ThreadFunction function;
        ThreadContext<ContextType> context;
        const char* name;
        ThreadPriority priority;
        RealTimeConstraints rtConstraints;
        uint32_t stackSize;
        uint32_t preferredCore;  // UINT32_MAX = don't care
    };

    // === Thread Creation === //

    // Create new thread with type-safe context
    // Template helper for type-safe context passing
    template<typename ContextType>
    [[nodiscard]] ThreadHandle createThread(ThreadCreateInfo<ContextType>& info) {
        // Create a type-erased unique_ptr that knows how to delete ContextType
        auto rawContext = info.context.release();
        void* contextPtr = rawContext.get();
        
        auto typeErasedContext = std::unique_ptr<void, void(*)(void*)>(
            rawContext.release(),
            [](void* p) { delete static_cast<ContextType*>(p); }
        );

        return createThreadImpl(info.function,
                              contextPtr,
                              std::move(typeErasedContext),
                              info.name,
                              info.priority,
                              info.rtConstraints,
                              info.stackSize,
                              info.preferredCore);
    }

    // === Thread Control === //

    // Update real-time constraints for running thread
    // Returns: true if constraints applied successfully
    // Thread-safety: Can be called from any thread
    [[nodiscard]] virtual bool setRealTimeConstraints(ThreadHandle handle,
                                       const RealTimeConstraints& constraints) = 0;

    // === Watchdog (Deadlock Detection) === //

    // Start watchdog timer for thread
    // If thread doesn't "ping" within timeoutNs, callback is invoked
    // Returns: Watchdog handle for stopping
    [[nodiscard]] virtual WatchdogHandle startWatchdog(ThreadHandle handle,
                                        uint64_t timeoutNs,
                                        WatchdogCallback callback) = 0;

    // Stop running watchdog
    // Precondition: handle must be valid (from startWatchdog)
    virtual void stopWatchdog(WatchdogHandle handle) = 0;

    // Reset watchdog timer for the current thread
    // This should be called periodically by the thread being monitored
    virtual void pulseWatchdog(WatchdogHandle handle) = 0;

    // Wait for thread to finish
    virtual void joinThread(ThreadHandle handle) = 0;
    
    // === Workgroup Management (Modern Real-Time Sync) === //

    // Join current thread to a real-time workgroup
    // Thread-safety: Must be called from the thread that wants to join
    virtual bool joinWorkgroup(WorkgroupHandle handle) = 0;

    // Leave the current workgroup
    // Thread-safety: Must be called from the thread that wants to leave
    virtual void leaveWorkgroup() = 0;

    // === Implementation === //

protected:
    virtual ThreadHandle createThreadImpl(ThreadFunction func,
                                        void* context,
                                        std::unique_ptr<void, void(*)(void*)> contextHolder,
                                        const char* name,
                                        ThreadPriority priority,
                                        const RealTimeConstraints& rt,
                                        uint32_t stackSize,
                                        uint32_t core) = 0;

public:
    static std::unique_ptr<IThreadManager> create();

    virtual ~IThreadManager() = default;
};

} // namespace Layer1
