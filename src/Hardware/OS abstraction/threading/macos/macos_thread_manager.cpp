// macos_thread_manager.cpp
// Layer 1: Hardware/OS Abstraction - macOS Thread Manager Implementation

#include "macos_thread_manager.h"
#include <pthread.h>
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <mach/mach_time.h>
#include <os/workgroup.h>

namespace Layer1 {
    
static thread_local os_workgroup_join_token_s s_wg_token;
static thread_local os_workgroup_t s_current_wg = nullptr;
static thread_local bool s_in_wg = false;

void MacOSThreadManager::applyThreadName(std::thread& /*thread*/, const char* name) {
    if (name) {
        // On macOS, pthread_setname_np only sets the name of the CURRENT thread
        pthread_setname_np(name);
    }
}

void MacOSThreadManager::applyThreadPriority(std::thread& thread, ThreadPriority priority, const RealTimeConstraints& rt) {
    if (priority == ThreadPriority::REALTIME || priority == ThreadPriority::TIME_CRITICAL) {
        // Mach real-time policy is handled via applyRealTimeConstraints
        // But we need a valid handle to call it.
        // For now, we rely on the fact that createThreadImpl will call applyRealTimeConstraints
        // if priority is RT. 
        // Actually, let's look at how we can do it with just native_handle.
        
        thread_time_constraint_policy_data_t policy;
        mach_timebase_info_data_t timebase;
        mach_timebase_info(&timebase);
        double ns_to_mach = static_cast<double>(timebase.denom) / static_cast<double>(timebase.numer);

        policy.period = static_cast<uint32_t>(rt.periodNs * ns_to_mach);
        policy.computation = static_cast<uint32_t>(rt.computationNs * ns_to_mach);
        policy.constraint = static_cast<uint32_t>(rt.deadlineNs * ns_to_mach);
        policy.preemptible = TRUE;

        thread_policy_set(
            pthread_mach_thread_np(thread.native_handle()),
            THREAD_TIME_CONSTRAINT_POLICY,
            reinterpret_cast<thread_policy_t>(&policy),
            THREAD_TIME_CONSTRAINT_POLICY_COUNT
        );
    } else {
        int policy = SCHED_OTHER;
        struct sched_param param;
        pthread_getschedparam(thread.native_handle(), &policy, &param);
        
        int min_prio = sched_get_priority_min(policy);
        int max_prio = sched_get_priority_max(policy);
        
        switch (priority) {
            case ThreadPriority::IDLE:   param.sched_priority = min_prio; break;
            case ThreadPriority::LOW:    param.sched_priority = (min_prio * 3 + max_prio) / 4; break;
            case ThreadPriority::NORMAL: param.sched_priority = (min_prio + max_prio) / 2; break;
            case ThreadPriority::HIGH:   param.sched_priority = (min_prio + max_prio * 3) / 4; break;
            default: break;
        }
        pthread_setschedparam(thread.native_handle(), policy, &param);
    }
}

void MacOSThreadManager::applyThreadAffinity(std::thread& thread, uint32_t core) {
    thread_affinity_policy_data_t policy_data = { static_cast<integer_t>(core) };
    thread_policy_set(pthread_mach_thread_np(thread.native_handle()),
                     THREAD_AFFINITY_POLICY,
                     reinterpret_cast<thread_policy_t>(&policy_data),
                     THREAD_AFFINITY_POLICY_COUNT);
}

bool MacOSThreadManager::applyRealTimeConstraints(ThreadHandle handle, const RealTimeConstraints& constraints) {
    // Note: handle validation already done in base class
    auto& thread = threads[handle.id]->thread;
    
    thread_time_constraint_policy_data_t policy;
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    double ns_to_mach = static_cast<double>(timebase.denom) / static_cast<double>(timebase.numer);

    policy.period = static_cast<uint32_t>(constraints.periodNs * ns_to_mach);
    policy.computation = static_cast<uint32_t>(constraints.computationNs * ns_to_mach);
    policy.constraint = static_cast<uint32_t>(constraints.deadlineNs * ns_to_mach);
    policy.preemptible = TRUE;

    kern_return_t kr = thread_policy_set(
        pthread_mach_thread_np(thread.native_handle()),
        THREAD_TIME_CONSTRAINT_POLICY,
        reinterpret_cast<thread_policy_t>(&policy),
        THREAD_TIME_CONSTRAINT_POLICY_COUNT
    );
    return kr == KERN_SUCCESS;
}

bool MacOSThreadManager::joinWorkgroup(WorkgroupHandle handle) {
    if (handle.type != 1 || !handle.isValid() || s_in_wg) return false;
    
    os_workgroup_t wg = reinterpret_cast<os_workgroup_t>(handle.handle);
    int res = os_workgroup_join(wg, &s_wg_token);
    if (res == 0) {
        s_current_wg = wg;
        s_in_wg = true;
        return true;
    }
    return false;
}

void MacOSThreadManager::leaveWorkgroup() {
    if (s_in_wg && s_current_wg) {
        os_workgroup_leave(s_current_wg, &s_wg_token);
        s_current_wg = nullptr;
        s_in_wg = false;
    }
}

} // namespace Layer1
