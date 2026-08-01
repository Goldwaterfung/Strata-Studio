// windows_thread_manager.cpp
// Layer 1: Hardware/OS Abstraction - Windows Thread Manager Implementation

#ifdef _WIN32

#include "windows_thread_manager.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <processthreadsapi.h>
#include <avrt.h>

#pragma comment(lib, "Avrt.lib")

namespace Layer1 {

static thread_local HANDLE s_mmcss_handle = NULL;

// Helper to convert char* to wchar_t* for Windows APIs
static std::wstring to_wstring(const char* str) {
    if (!str) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str, -1, &result[0], len);
    return result;
}

void WindowsThreadManager::applyThreadName(std::thread& thread, const char* name) {
    if (name) {
        std::wstring wname = to_wstring(name);
        SetThreadDescription(thread.native_handle(), wname.c_str());
    }
}

void WindowsThreadManager::applyThreadPriority(std::thread& thread, ThreadPriority priority, const RealTimeConstraints& /*rt*/) {
    int winPrio = THREAD_PRIORITY_NORMAL;
    
    switch (priority) {
        case ThreadPriority::IDLE:          winPrio = THREAD_PRIORITY_IDLE; break;
        case ThreadPriority::LOW:           winPrio = THREAD_PRIORITY_LOWEST; break;
        case ThreadPriority::NORMAL:        winPrio = THREAD_PRIORITY_NORMAL; break;
        case ThreadPriority::HIGH:          winPrio = THREAD_PRIORITY_ABOVE_NORMAL; break;
        case ThreadPriority::REALTIME:      winPrio = THREAD_PRIORITY_HIGHEST; break;
        case ThreadPriority::TIME_CRITICAL: winPrio = THREAD_PRIORITY_TIME_CRITICAL; break;
    }
    
    SetThreadPriority(thread.native_handle(), winPrio);
}

void WindowsThreadManager::applyThreadAffinity(std::thread& thread, uint32_t core) {
    SetThreadAffinityMask(thread.native_handle(), (DWORD_PTR)1 << core);
}

bool WindowsThreadManager::applyRealTimeConstraints(ThreadHandle /*handle*/, const RealTimeConstraints& /*constraints*/) {
    // Rely on joinWorkgroup for MMCSS
    return true;
}

bool WindowsThreadManager::joinWorkgroup(WorkgroupHandle handle) {
    if (handle.type != 2 || !handle.isValid() || s_mmcss_handle) return false;
    
    // On Windows, the "handle" is actually a pointer to a wide string for the task name
    const wchar_t* taskName = reinterpret_cast<const wchar_t*>(handle.handle);
    DWORD dummy = 0;
    s_mmcss_handle = AvSetMmThreadCharacteristicsW(taskName, &dummy);
    
    return s_mmcss_handle != NULL;
}

void WindowsThreadManager::leaveWorkgroup() {
    if (s_mmcss_handle) {
        AvRevertMmThreadCharacteristics(s_mmcss_handle);
        s_mmcss_handle = NULL;
    }
}

} // namespace Layer1

#endif // _WIN32
