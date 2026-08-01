// thread_manager_base.cpp
// Layer 1: Hardware/OS Abstraction - Common Thread Manager Base

#include "thread_manager_base.h"
#include <chrono>
#include <algorithm>
#include <future>

namespace Layer1 {

ThreadManagerBase::ThreadManagerBase() {
    watchdogThread = std::thread(&ThreadManagerBase::watchdogWorker, this);
}

void ThreadManagerBase::shutdownThreads() {
    // 1. Signal watchdog to shutdown
    {
        std::lock_guard<std::mutex> lock(watchdogMutex);
        watchdogShutdown = true;
    }
    watchdogCV.notify_all();
    if (watchdogThread.joinable()) watchdogThread.join();

    // 2. Collect and join all threads
    std::vector<std::thread> threadsToJoin;
    {
        std::lock_guard<std::mutex> lock(threadsMutex);
        for (auto& entry : threads) {
            if (entry && entry->thread.joinable()) {
                threadsToJoin.push_back(std::move(entry->thread));
            }
        }
    }

    for (auto& t : threadsToJoin) {
        t.join();
    }
}

ThreadManagerBase::~ThreadManagerBase() {
    // Final safety check - threads should have been joined by derived class
    shutdownThreads();
}

bool ThreadManagerBase::setRealTimeConstraints(ThreadHandle handle, const RealTimeConstraints& constraints) {
    std::lock_guard<std::mutex> lock(threadsMutex);
    if (!isValidHandleInternal(handle)) return false;
    
    threads[handle.id]->constraints = constraints;
    return applyRealTimeConstraints(handle, constraints);
}

IThreadManager::WatchdogHandle ThreadManagerBase::startWatchdog(ThreadHandle handle, uint64_t timeoutNs, IThreadManager::WatchdogCallback callback) {
    std::lock_guard<std::mutex> lock(watchdogMutex);
    uint32_t id = nextWatchdogId++;
    WatchdogEntry entry;
    entry.handle = id;
    entry.threadHandle = handle;
    entry.timeoutNs = timeoutNs;
    entry.callback = callback;
    entry.active = true;
    watchdogs[id] = entry;
    return id;
}

void ThreadManagerBase::stopWatchdog(IThreadManager::WatchdogHandle handle) {
    std::lock_guard<std::mutex> lock(watchdogMutex);
    watchdogs.erase(handle);
}

void ThreadManagerBase::pulseWatchdog(IThreadManager::WatchdogHandle handle) {
    // Current time in NS
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    uint64_t nowNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());

    std::lock_guard<std::mutex> lock(watchdogMutex);
    auto it = watchdogs.find(handle);
    if (it != watchdogs.end()) {
        ThreadHandle th = it->second.threadHandle;
        std::lock_guard<std::mutex> tLock(threadsMutex);
        if (isValidHandleInternal(th)) {
            threads[th.id]->lastPulseNs.store(nowNs, std::memory_order_relaxed);
        }
    }
}

void ThreadManagerBase::joinThread(ThreadHandle handle) {
    std::unique_lock<std::mutex> lock(threadsMutex);
    if (handle.id < threads.size() && threads[handle.id] && threads[handle.id]->handle.generation == handle.generation) {
        auto& entry = *threads[handle.id];
        if (entry.thread.joinable()) {
            lock.unlock();
            entry.thread.join();
            lock.lock();
        }
        
        // Once joined, it's safe to mark inactive and free the slot
        if (threads[handle.id]->active == false) {
            // Already finished execution, safe to free
            freeSlots.push_back(handle.id);
        } else {
            // Mark as inactive so the slot can be reused later
            threads[handle.id]->active = false;
            freeSlots.push_back(handle.id);
        }
    }
}

ThreadHandle ThreadManagerBase::createThreadImpl(ThreadFunction func,
                                            void* context,
                                            std::unique_ptr<void, void(*)(void*)> contextHolder,
                                            const char* name,
                                            ThreadPriority priority,
                                            const RealTimeConstraints& rt,
                                            uint32_t /*stackSize*/,
                                            uint32_t core) {
    std::lock_guard<std::mutex> lock(threadsMutex);

    uint32_t id;
    uint32_t generation;

    if (freeSlots.empty()) {
        id = static_cast<uint32_t>(threads.size());
        generation = 1;
        threads.push_back(std::make_unique<ThreadEntry>());
    } else {
        id = freeSlots.back();
        freeSlots.pop_back();
        generation = threads[id]->handle.generation + 1;
        if (generation == 0) generation = 1;
        
        // Ensure old thread object is cleaned up before re-assignment to prevent std::terminate()
        if (threads[id]->thread.joinable()) {
            threads[id]->thread.join();
        }
    }

    auto& entry = *threads[id];
    entry.handle = {id, generation};
    entry.constraints = rt;
    entry.active = true;
    entry.lastPulseNs.store(0);
    entry.contextHolder = std::move(contextHolder);

    // Use a promise to block the child thread until priority and affinity are applied
    auto setupPromise = std::make_shared<std::promise<void>>();
    auto setupFuture = setupPromise->get_future();

    ThreadEntry* entryPtr = threads[id].get();

    entry.thread = std::thread([this, func, context, name, id, generation, entryPtr, setupFuture = std::move(setupFuture)]() mutable {
        // Wait for parent thread to apply priority/affinity to avoid priority inversion
        setupFuture.wait();

        // Platform specific name application
        applyThreadName(entryPtr->thread, name);

        // Set initial pulse
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        entryPtr->lastPulseNs.store(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count()));

        func(context);
        
        std::lock_guard<std::mutex> innerLock(threadsMutex);
        // Verify we are still the same thread (slot hasn't been reused)
        if (id < threads.size() && threads[id] && threads[id]->handle.generation == generation) {
            threads[id]->active = false;
        }
    });

    // Apply platform-specific settings before the thread starts executing its workload
    applyThreadPriority(entry.thread, priority, rt);
    
    if (core != UNUSED_CORE) {
        applyThreadAffinity(entry.thread, core);
    }

    // Unblock the child thread now that OS constraints are applied
    setupPromise->set_value();

    return entry.handle;
}

void ThreadManagerBase::watchdogWorker() {
    while (!watchdogShutdown) {
        {
            std::unique_lock<std::mutex> lock(watchdogMutex);
            watchdogCV.wait_for(lock, std::chrono::milliseconds(100), [this] { return watchdogShutdown.load(); });
            if (watchdogShutdown) break;

            auto now = std::chrono::steady_clock::now().time_since_epoch();
            uint64_t nowNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());

            for (auto& pair : watchdogs) {
                auto& entry = pair.second;
                if (!entry.active) continue;

                std::lock_guard<std::mutex> tLock(threadsMutex);
                if (isValidHandleInternal(entry.threadHandle)) {
                    auto& thread = *threads[entry.threadHandle.id];
                    uint64_t lastPulse = thread.lastPulseNs.load(std::memory_order_relaxed);
                    if (lastPulse != 0 && (nowNs - lastPulse) > entry.timeoutNs) {
                        IThreadManager::WatchdogCallback cb = entry.callback;
                        if (cb) cb(entry.threadHandle);
                    }
                }
            }
        }
    }
}

bool ThreadManagerBase::isValidHandleInternal(ThreadHandle handle) const {
    return handle.id < threads.size() && 
           threads[handle.id] &&
           threads[handle.id]->handle.generation == handle.generation &&
           threads[handle.id]->active;
}

} // namespace Layer1
