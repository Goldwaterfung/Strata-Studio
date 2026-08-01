// src/Core audio engine/scheduler/offline_dsp_thread_pool.h
#pragma once

#include "Core infrastructure/bridges/mpsc_queue.h"
#include <atomic>
#include <functional>
#include <thread>
#include <queue>
#include <vector>
#include <memory>

#ifdef __APPLE__
#include <dispatch/dispatch.h>
#else
#include <semaphore.h>
#endif

namespace Layer3 {

class IAudioEngine;

class OfflineDSPThreadPool {
public:
    // Initialize with a reference to the audio engine (for CPU telemetry)
    // and the maximum number of worker threads to allocate.
    explicit OfflineDSPThreadPool(IAudioEngine* engine, uint32_t maxThreads = 4);
    ~OfflineDSPThreadPool();

    // Enqueue an offline DSP task (e.g. RubberBand time-stretching chunk)
    void enqueueTask(std::function<void()> task);

private:
    void supervisorLoop();
    void workerLoop(uint32_t workerIndex);

    IAudioEngine* audioEngine_;
    uint32_t maxThreads_;
    std::atomic<bool> isRunning_;

    // Thread management
    std::thread supervisorThread_;
    std::vector<std::thread> workerThreads_;
    std::atomic<uint32_t> activeThreadCount_{1};

    std::queue<std::function<void()>> taskQueue_;
    std::mutex taskQueueMutex_;
    std::atomic<uint32_t> pendingTaskCount_{0};

    // Supervisor wake mechanism
#ifdef __APPLE__
    dispatch_semaphore_t supervisorSemaphore_;
    std::vector<dispatch_semaphore_t> workerSemaphores_;
#else
    sem_t supervisorSemaphore_;
    std::vector<std::unique_ptr<sem_t>> workerSemaphores_;
#endif

    // Tasks dispatched to workers
    struct WorkerState {
        std::function<void()> currentTask;
        std::atomic<bool> hasTask{false};
        std::atomic<bool> isIdle{true};
    };
    std::vector<std::unique_ptr<WorkerState>> workerStates_;
};

} // namespace Layer3
