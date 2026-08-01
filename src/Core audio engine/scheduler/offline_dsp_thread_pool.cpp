// src/Core audio engine/scheduler/offline_dsp_thread_pool.cpp
#include "offline_dsp_thread_pool.h"
#include "../engine/iaudio_engine.h"
#include <chrono>
#include <iostream>

#ifdef __APPLE__
#include <pthread.h>
#endif

namespace Layer3 {

OfflineDSPThreadPool::OfflineDSPThreadPool(IAudioEngine* engine, uint32_t maxThreads)
    : audioEngine_(engine)
    , maxThreads_(maxThreads)
    , isRunning_(true)
{
#ifdef __APPLE__
    supervisorSemaphore_ = dispatch_semaphore_create(0);
    for (uint32_t i = 0; i < maxThreads_; ++i) {
        workerSemaphores_.push_back(dispatch_semaphore_create(0));
        workerStates_.push_back(std::make_unique<WorkerState>());
    }
#else
    sem_init(&supervisorSemaphore_, 0, 0);
    for (uint32_t i = 0; i < maxThreads_; ++i) {
        workerSemaphores_.push_back(std::make_unique<sem_t>());
        sem_init(workerSemaphores_.back().get(), 0, 0);
        workerStates_.push_back(std::make_unique<WorkerState>());
    }
#endif

    // Start workers
    for (uint32_t i = 0; i < maxThreads_; ++i) {
        workerThreads_.emplace_back(&OfflineDSPThreadPool::workerLoop, this, i);
    }

    // Start supervisor
    supervisorThread_ = std::thread(&OfflineDSPThreadPool::supervisorLoop, this);
}

OfflineDSPThreadPool::~OfflineDSPThreadPool() {
    isRunning_.store(false, std::memory_order_release);

    // Wake all to exit
#ifdef __APPLE__
    dispatch_semaphore_signal(supervisorSemaphore_);
    for (auto& sem : workerSemaphores_) {
        dispatch_semaphore_signal(sem);
    }
#else
    sem_post(&supervisorSemaphore_);
    for (auto& sem : workerSemaphores_) {
        sem_post(sem.get());
    }
#endif

    if (supervisorThread_.joinable()) {
        supervisorThread_.join();
    }
    for (auto& t : workerThreads_) {
        if (t.joinable()) {
            t.join();
        }
    }

#ifdef __APPLE__
    dispatch_release(supervisorSemaphore_);
    for (auto& sem : workerSemaphores_) {
        dispatch_release(sem);
    }
#else
    sem_destroy(&supervisorSemaphore_);
    for (auto& sem : workerSemaphores_) {
        sem_destroy(sem.get());
    }
#endif
}

void OfflineDSPThreadPool::enqueueTask(std::function<void()> task) {
    if (!task) return;
    
    {
        std::lock_guard<std::mutex> lock(taskQueueMutex_);
        taskQueue_.push(std::move(task));
    }
    pendingTaskCount_.fetch_add(1, std::memory_order_relaxed);
#ifdef __APPLE__
    dispatch_semaphore_signal(supervisorSemaphore_);
#else
    sem_post(&supervisorSemaphore_);
#endif
}

void OfflineDSPThreadPool::supervisorLoop() {
#ifdef __APPLE__
    // Supervisor can run on Utility E-cores to save power
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif

    while (isRunning_.load(std::memory_order_relaxed)) {
#ifdef __APPLE__
        dispatch_semaphore_wait(supervisorSemaphore_, DISPATCH_TIME_FOREVER);
#else
        sem_wait(&supervisorSemaphore_);
#endif
        if (!isRunning_.load(std::memory_order_relaxed)) break;

        // Pull task from queue
        std::function<void()> task;
        bool hasTask = false;
        {
            std::lock_guard<std::mutex> lock(taskQueueMutex_);
            if (!taskQueue_.empty()) {
                task = std::move(taskQueue_.front());
                taskQueue_.pop();
                hasTask = true;
            }
        }
        
        if (hasTask) {
            pendingTaskCount_.fetch_sub(1, std::memory_order_relaxed);

            // CPU Telemetry Check
            double currentRtCpuLoad = audioEngine_ ? audioEngine_->getCpuLoad() : 0.0;
            
            // Dynamic sizing based on telemetry
            // Min 1 thread, scale up if load < 15% and queue is growing
            uint32_t activeThreads = activeThreadCount_.load(std::memory_order_relaxed);
            uint32_t pending = pendingTaskCount_.load(std::memory_order_relaxed);
            
            if (pending > 0 && currentRtCpuLoad < 0.15 && activeThreads < maxThreads_) {
                activeThreads++;
                activeThreadCount_.store(activeThreads, std::memory_order_relaxed);
            } else if (pending == 0 && activeThreads > 1) {
                // Scale down if no pending tasks
                activeThreads--;
                activeThreadCount_.store(activeThreads, std::memory_order_relaxed);
            }

            // Find an idle worker (within the active thread limit)
            bool dispatched = false;
            while (!dispatched && isRunning_.load(std::memory_order_relaxed)) {
                for (uint32_t i = 0; i < activeThreads; ++i) {
                    if (workerStates_[i]->isIdle.load(std::memory_order_acquire)) {
                        workerStates_[i]->isIdle.store(false, std::memory_order_release);
                        workerStates_[i]->currentTask = std::move(task);
                        workerStates_[i]->hasTask.store(true, std::memory_order_release);
                        
#ifdef __APPLE__
                        dispatch_semaphore_signal(workerSemaphores_[i]);
#else
                        sem_post(workerSemaphores_[i].get());
#endif
                        dispatched = true;
                        break;
                    }
                }

                if (!dispatched) {
                    // All allowed active threads are busy. 
                    // Yield and try again to avoid spinning the supervisor heavily.
                    std::this_thread::yield();
                }
            }
        }
    }
}

void OfflineDSPThreadPool::workerLoop(uint32_t workerIndex) {
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif

    auto& state = *workerStates_[workerIndex];

    while (isRunning_.load(std::memory_order_relaxed)) {
#ifdef __APPLE__
        dispatch_semaphore_wait(workerSemaphores_[workerIndex], DISPATCH_TIME_FOREVER);
#else
        sem_wait(workerSemaphores_[workerIndex].get());
#endif
        if (!isRunning_.load(std::memory_order_relaxed)) break;

        if (state.hasTask.load(std::memory_order_acquire)) {
            if (state.currentTask) {
                state.currentTask();
                state.currentTask = nullptr; // Free resources
            }
            state.hasTask.store(false, std::memory_order_release);
            state.isIdle.store(true, std::memory_order_release);
            
            // Signal supervisor that a worker is free in case it's waiting
#ifdef __APPLE__
            dispatch_semaphore_signal(supervisorSemaphore_);
#else
            sem_post(&supervisorSemaphore_);
#endif
        }
    }
}

} // namespace Layer3
