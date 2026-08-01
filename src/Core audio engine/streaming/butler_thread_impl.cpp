#include "butler_thread_impl.h"
#include <algorithm>
#include <unordered_set>
#include "../Hardware/OS abstraction/threading/ithread_manager.h"

#ifdef __APPLE__
#include <pthread.h>
#endif

namespace Layer3 {

ButlerThreadImpl::ButlerThreadImpl()
    : isRunning(false)
    , state(ButlerState::IDLE)
    , pendingCount(0)
    , filesystem(nullptr)
{
    // Initialize semaphore with initial value 0 (binary semaphore for signaling)
#ifdef __APPLE__
    wakeSemaphore = dispatch_semaphore_create(0);
#else
    sem_init(&wakeSemaphore, 0, 0);
#endif
}

ButlerThreadImpl::~ButlerThreadImpl() {
    stop();
    if (filesystem) {
        std::lock_guard<std::mutex> lock(activeFilesMutex_);
        for (const auto& f : activeFiles_) {
            if (f.handle != Layer1::INVALID_FILE_HANDLE) {
                filesystem->closeFile(f.handle);
            }
        }
    }
#ifdef __APPLE__
    dispatch_release(wakeSemaphore);
#else
    sem_destroy(&wakeSemaphore);
#endif
}

bool ButlerThreadImpl::start(Layer1::WorkgroupHandle workgroupHandle) {
    if (isRunning.exchange(true)) return true;
    
    workgroupHandle_ = workgroupHandle;

    worker = std::thread(&ButlerThreadImpl::threadLoop, this);
    return true;
}

void ButlerThreadImpl::stop() {
    if (!isRunning.exchange(false)) return;

    // Post to semaphore to wake the thread so it can exit cleanly
#ifdef __APPLE__
    dispatch_semaphore_signal(wakeSemaphore);
#else
    sem_post(&wakeSemaphore);
#endif
    if (worker.joinable()) {
        worker.join();
    }
}

void ButlerThreadImpl::attachFileSystem(Layer1::IFileSystem* fs) {
    filesystem = fs;
}

bool ButlerThreadImpl::registerBuffer(IStreamingBuffer* buffer) {
    if (!buffer) return false;
    std::lock_guard<std::mutex> lock(bufferMutex);
    float sr = sampleRate_.load(std::memory_order_relaxed);
    if (sr > 0.0f) {
        buffer->setSampleRate(static_cast<uint32_t>(sr));
    } else if (buffer->getSampleRate() > 0) {
        sampleRate_.store(static_cast<float>(buffer->getSampleRate()), std::memory_order_release);
    }
    registeredBuffers.push_back(buffer);
    return true;
}

bool ButlerThreadImpl::unregisterBuffer(IStreamingBuffer* buffer) {
    std::lock_guard<std::mutex> lock(bufferMutex);
    
    // Clear from track mappings
    for (size_t i = 0; i < MAX_TRACK_MAPPINGS; ++i) {
        if (trackMappings_[i].buffer.load(std::memory_order_acquire) == buffer) {
            trackMappings_[i].buffer.store(nullptr, std::memory_order_release);
            trackMappings_[i].trackId.store(0, std::memory_order_release);
        }
    }
    
    // Clear from region mappings
    for (size_t i = 0; i < MAX_REGION_MAPPINGS; ++i) {
        if (regionMappings_[i].buffer.load(std::memory_order_acquire) == buffer) {
            regionMappings_[i].buffer.store(nullptr, std::memory_order_release);
            regionMappings_[i].sourceId.store(0, std::memory_order_release);
            regionMappings_[i].regionId.store(0, std::memory_order_release);
        }
    }

    auto it = std::find(registeredBuffers.begin(), registeredBuffers.end(), buffer);
    if (it != registeredBuffers.end()) {
        registeredBuffers.erase(it);
        return true;
    }
    return false;
}

void ButlerThreadImpl::wakeButler() {
    // RT-safe: sem_post/dispatch_semaphore_signal is async-signal-safe
    // Increment pending count to track that a buffer needs refill
    pendingCount.fetch_add(1, std::memory_order_relaxed);
#ifdef __APPLE__
    dispatch_semaphore_signal(wakeSemaphore);
#else
    sem_post(&wakeSemaphore);
#endif
}

void ButlerThreadImpl::scheduleTask(std::function<void()> task) {
    if (!task) return;
    {
        std::lock_guard<std::mutex> lock(taskMutex);
        taskQueue.push_back(std::move(task));
    }
    wakeButler();
}

uint32_t ButlerThreadImpl::getPendingBufferCount() const {
    // RT-safe: atomic read of pending refill count
    return pendingCount.load(std::memory_order_relaxed);
}

void ButlerThreadImpl::threadLoop() {
#ifdef __APPLE__
    // macOS: explicit QoS class for streaming buffer refill (Utility class schedules on E-Cores)
    pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif

    // Join the realtime audio workgroup if provided (macOS frequency scaling integration)
    if (workgroupHandle_.handle != Layer1::WorkgroupHandle::invalid().handle) {
        Layer1::IThreadManager::create()->joinWorkgroup(workgroupHandle_);
    }

    while (isRunning.load(std::memory_order_relaxed)) {
        // RT-safe wait: sem_wait/dispatch_semaphore_wait blocks without holding internal mutexes
#ifdef __APPLE__
        dispatch_semaphore_wait(wakeSemaphore, DISPATCH_TIME_FOREVER);
#else
        sem_wait(&wakeSemaphore);
#endif

        if (!isRunning.load(std::memory_order_relaxed)) break;

        state.store(ButlerState::WORKING, std::memory_order_relaxed);

        // 1. Load active playback transport state and snapshot atomically
        uint64_t currentTransportPos = transportPos_.load(std::memory_order_acquire);
        float sampleRate = sampleRate_.load(std::memory_order_acquire);
        const TimelineSnapshot* snapshot = activeSnapshot_.load(std::memory_order_acquire);

        // Calculate lookahead window (2.0 seconds cushion)
        uint64_t lookaheadWindowSize = static_cast<uint64_t>(sampleRate * 2.0f);
        uint64_t lookaheadEnd = currentTransportPos + lookaheadWindowSize;

        // Caching active track mappings and source mappings locally (reduces memory barriers)
        struct DenseTrackMapping {
            uint32_t trackId;
            IStreamingBuffer* buffer;
            size_t originalIndex;
        };
        std::vector<DenseTrackMapping> activeTracks;
        activeTracks.reserve(32);
        std::unordered_set<IStreamingBuffer*> trackMappedBuffers;
        
        for (size_t i = 0; i < MAX_TRACK_MAPPINGS; ++i) {
            uint32_t trackId = trackMappings_[i].trackId.load(std::memory_order_acquire);
            IStreamingBuffer* buffer = trackMappings_[i].buffer.load(std::memory_order_acquire);
            if (trackId != 0 && buffer != nullptr) {
                activeTracks.push_back({trackId, buffer, i});
                trackMappedBuffers.insert(buffer);
            }
        }

        std::unordered_map<uint64_t, IStreamingBuffer*> regionIdToBuffer;
        std::unordered_map<IStreamingBuffer*, uint64_t> bufferToRegionId;
        std::unordered_map<IStreamingBuffer*, uint32_t> bufferToSourceId;
        std::unordered_map<IStreamingBuffer*, size_t> bufferToRegionMappingIdx;
        
        for (size_t sm = 0; sm < MAX_REGION_MAPPINGS; ++sm) {
            uint64_t regionId = regionMappings_[sm].regionId.load(std::memory_order_acquire);
            uint32_t sourceId = regionMappings_[sm].sourceId.load(std::memory_order_acquire);
            IStreamingBuffer* buffer = regionMappings_[sm].buffer.load(std::memory_order_acquire);
            if (regionId != 0 && buffer != nullptr) {
                regionIdToBuffer[regionId] = buffer;
                bufferToRegionId[buffer] = regionId;
                bufferToSourceId[buffer] = sourceId;
                bufferToRegionMappingIdx[buffer] = sm;
            }
        }

        std::unordered_map<uint32_t, std::vector<IStreamingBuffer*>> trackBuffers;
        for (const auto& mapping : activeTracks) {
            trackBuffers[mapping.trackId].push_back(mapping.buffer);
        }

        for (const auto& pair : trackBuffers) {
            uint32_t trackId = pair.first;
            const auto& buffers = pair.second;

            // Collect up to MAX_BUFFERS_PER_TRACK top regions
            std::vector<const SnapshotRegion*> trackRegions;
            if (snapshot) {
                for (uint32_t r = 0; r < snapshot->regionCount; ++r) {
                    const auto& region = snapshot->regions[r];
                    if (region.trackId.id == trackId && region.type == RegionType::AUDIO && !region.isMuted) {
                        uint64_t regStart = region.positionSample;
                        uint64_t regEnd = regStart + region.durationProjectFrames;
                        if ((currentTransportPos >= regStart && currentTransportPos < regEnd) ||
                            (regStart > currentTransportPos && regStart <= lookaheadEnd)) {
                            trackRegions.push_back(&region);
                        }
                    }
                }
                // Sort by distance to currentTransportPos (active regions first, then nearest upcoming)
                std::sort(trackRegions.begin(), trackRegions.end(), [currentTransportPos](const SnapshotRegion* a, const SnapshotRegion* b) {
                    bool aActive = (currentTransportPos >= a->positionSample && currentTransportPos < a->positionSample + a->durationProjectFrames);
                    bool bActive = (currentTransportPos >= b->positionSample && currentTransportPos < b->positionSample + b->durationProjectFrames);
                    if (aActive && !bActive) return true;
                    if (!aActive && bActive) return false;
                    return a->positionSample < b->positionSample;
                });
                if (trackRegions.size() > buffers.size()) {
                    trackRegions.resize(buffers.size());
                }
            }

            // Assign regions to buffers. Try to keep existing assignments.
            std::unordered_set<IStreamingBuffer*> usedBuffers;
            
            for (const SnapshotRegion* region : trackRegions) {
                uint64_t targetRegionId = region->regionId.toRaw();
                uint32_t targetSourceId = region->sourceId;

                IStreamingBuffer* assignedBuffer = nullptr;
                auto rit = regionIdToBuffer.find(targetRegionId);
                if (rit != regionIdToBuffer.end() && std::find(buffers.begin(), buffers.end(), rit->second) != buffers.end()) {
                    assignedBuffer = rit->second;
                } else {
                    // Find an unused buffer
                    for (IStreamingBuffer* buf : buffers) {
                        if (usedBuffers.find(buf) == usedBuffers.end()) {
                            assignedBuffer = buf;
                            break;
                        }
                    }
                }
                
                if (!assignedBuffer) continue; // Should not happen
                usedBuffers.insert(assignedBuffer);

                IStreamingBuffer* currentMapped = rit != regionIdToBuffer.end() ? rit->second : nullptr;

                if (currentMapped != assignedBuffer) {
                    // Evict this buffer from any other region mappings
                    auto bit = bufferToRegionMappingIdx.find(assignedBuffer);
                    if (bit != bufferToRegionMappingIdx.end()) {
                        size_t sm = bit->second;
                        uint64_t oldRegionId = bufferToRegionId[assignedBuffer];
                        if (oldRegionId != 0) {
                            regionMappings_[sm].buffer.store(nullptr, std::memory_order_release);
                            regionMappings_[sm].sourceId.store(0, std::memory_order_release);
                            regionMappings_[sm].regionId.store(0, std::memory_order_release);

                            // Close file handle associated with old source
                            std::lock_guard<std::mutex> filesLock(activeFilesMutex_);
                            auto fit = std::find_if(activeFiles_.begin(), activeFiles_.end(),
                                [assignedBuffer](const ActiveFileHandleMapping& m) { return m.buffer == assignedBuffer; });
                            if (fit != activeFiles_.end()) {
                                if (fit->handle != Layer1::INVALID_FILE_HANDLE && filesystem) {
                                    filesystem->closeFile(fit->handle);
                                }
                                activeFiles_.erase(fit);
                            }
                            
                            // Update local maps
                            regionIdToBuffer.erase(oldRegionId);
                            bufferToRegionId.erase(assignedBuffer);
                            bufferToSourceId.erase(assignedBuffer);
                            bufferToRegionMappingIdx.erase(assignedBuffer);
                        }
                    }

                    // Map to new regionId
                    registerBufferForRegion(targetRegionId, targetSourceId, assignedBuffer);
                    
                    // Update local maps
                    size_t targetSmIdx = MAX_REGION_MAPPINGS;
                    for (size_t i = 0; i < MAX_REGION_MAPPINGS; ++i) {
                        if (regionMappings_[i].regionId.load(std::memory_order_relaxed) == targetRegionId) {
                            targetSmIdx = i;
                            break;
                        }
                    }
                    if (targetSmIdx < MAX_REGION_MAPPINGS) {
                        regionIdToBuffer[targetRegionId] = assignedBuffer;
                        bufferToRegionId[assignedBuffer] = targetRegionId;
                        bufferToSourceId[assignedBuffer] = targetSourceId;
                        bufferToRegionMappingIdx[assignedBuffer] = targetSmIdx;
                    }

                    // Resolve path for targetSourceId
                    char filePath[512] = {0};
                    bool pathFound = false;
                    {
                        std::lock_guard<std::mutex> pathLock(sourcePathsMutex_);
                        auto pit = std::find_if(sourcePaths_.begin(), sourcePaths_.end(),
                            [targetSourceId](const SourcePathMapping& m) { return m.sourceId == targetSourceId; });
                        if (pit != sourcePaths_.end()) {
                            std::strncpy(filePath, pit->filePath, sizeof(filePath) - 1);
                            pathFound = true;
                        }
                    }

                    if (pathFound && filesystem) {
                        Layer1::FileHandle newHandle = filesystem->openFile(filePath, true);
                        if (newHandle != Layer1::INVALID_FILE_HANDLE) {
                            {
                                std::lock_guard<std::mutex> filesLock(activeFilesMutex_);
                                activeFiles_.push_back({assignedBuffer, newHandle});
                            }
                            assignedBuffer->associateFile(newHandle);

                            // Calculate starting read position offset
                            uint64_t timelineDelta = 0;
                            if (currentTransportPos >= region->positionSample) {
                                timelineDelta = currentTransportPos - region->positionSample;
                            }
                            assignedBuffer->setPlaybackRatio(region->playbackRatio);
                            assignedBuffer->setTimelineOffset(timelineDelta, region->sourceStartSample);
                            assignedBuffer->requestRefill(timelineDelta);
                        }
                    }
                } else {
                    assignedBuffer->setPlaybackRatio(region->playbackRatio);
                }
            }

            // For any buffer in buffers that wasn't used, evict it
            for (IStreamingBuffer* buffer : buffers) {
                if (usedBuffers.find(buffer) == usedBuffers.end()) {
                    auto bit = bufferToRegionMappingIdx.find(buffer);
                    if (bit != bufferToRegionMappingIdx.end()) {
                        size_t sm = bit->second;
                        uint64_t oldRegionId = bufferToRegionId[buffer];
                        if (oldRegionId != 0) {
                            regionMappings_[sm].buffer.store(nullptr, std::memory_order_release);
                            regionMappings_[sm].sourceId.store(0, std::memory_order_release);
                            regionMappings_[sm].regionId.store(0, std::memory_order_release);

                            std::lock_guard<std::mutex> filesLock(activeFilesMutex_);
                            auto fit = std::find_if(activeFiles_.begin(), activeFiles_.end(),
                                [buffer](const ActiveFileHandleMapping& m) { return m.buffer == buffer; });
                            if (fit != activeFiles_.end()) {
                                if (fit->handle != Layer1::INVALID_FILE_HANDLE && filesystem) {
                                    filesystem->closeFile(fit->handle);
                                }
                                activeFiles_.erase(fit);
                            }
                            buffer->flushAsync();
                            
                            // Update local maps
                            regionIdToBuffer.erase(oldRegionId);
                            bufferToRegionId.erase(buffer);
                            bufferToSourceId.erase(buffer);
                            bufferToRegionMappingIdx.erase(buffer);
                        }
                    }
                }
            }
        }

        // Process active buffer refills (optimized lookup)
        std::vector<IStreamingBuffer*> buffersToProcess;
        {
            std::lock_guard<std::mutex> lock(bufferMutex);
            buffersToProcess = registeredBuffers;
        }

        for (auto* buffer : buffersToProcess) {
            bool isTrackMapped = (trackMappedBuffers.count(buffer) > 0);

            if (!isTrackMapped) {
                // Standalone buffer: refill normally
                uint64_t readPos = buffer->getReadPosition();
                buffer->refillAsync(readPos, filesystem);
            } else {
                // Track-mapped buffer: refill only if currently mapped to a region
                uint64_t mappedRegionId = 0;
                auto sit = bufferToRegionId.find(buffer);
                if (sit != bufferToRegionId.end()) {
                    mappedRegionId = sit->second;
                }

                if (mappedRegionId != 0) {
                    uint64_t readPos = buffer->getReadPosition();
                    buffer->refillAsync(readPos, filesystem);
                }
            }
        }

        // Process Generic Tasks
        std::vector<std::function<void()>> currentTasks;
        {
            std::lock_guard<std::mutex> lock(taskMutex);
            currentTasks.swap(taskQueue);
        }

        for (auto& task : currentTasks) {
            task();
        }

        // Decrement pending count once per wake cycle
        if (pendingCount.load(std::memory_order_relaxed) > 0) {
            pendingCount.fetch_sub(1, std::memory_order_relaxed);
        }

        state.store(ButlerState::IDLE, std::memory_order_relaxed);
    }

    state.store(ButlerState::SHUTTING_DOWN, std::memory_order_relaxed);
}

bool ButlerThreadImpl::registerBufferForTrack(uint32_t trackId, IStreamingBuffer* buffer) {
    if (trackId == 0) return false;
    for (size_t i = 0; i < MAX_TRACK_MAPPINGS; ++i) {
        uint32_t expected = 0;
        if (trackMappings_[i].trackId.compare_exchange_strong(expected, trackId, std::memory_order_acq_rel)) {
            trackMappings_[i].buffer.store(buffer, std::memory_order_release);
            return true;
        }
        if (expected == trackId) {
            trackMappings_[i].buffer.store(buffer, std::memory_order_release);
            return true;
        }
    }
    return false;
}

bool ButlerThreadImpl::unregisterBufferForTrack(uint32_t trackId) {
    if (trackId == 0) return false;
    bool found = false;
    for (size_t i = 0; i < MAX_TRACK_MAPPINGS; ++i) {
        if (trackMappings_[i].trackId.load(std::memory_order_acquire) == trackId) {
            trackMappings_[i].buffer.store(nullptr, std::memory_order_release);
            trackMappings_[i].trackId.store(0, std::memory_order_release);
            found = true;
        }
    }
    return found;
}

void ButlerThreadImpl::registerSourcePath(uint32_t sourceId, const char* filePath) {
    if (sourceId == 0 || !filePath) return;
    std::lock_guard<std::mutex> lock(sourcePathsMutex_);
    for (auto& mapping : sourcePaths_) {
        if (mapping.sourceId == sourceId) {
            std::strncpy(mapping.filePath, filePath, sizeof(mapping.filePath) - 1);
            mapping.filePath[sizeof(mapping.filePath) - 1] = '\0';
            return;
        }
    }
    SourcePathMapping mapping;
    mapping.sourceId = sourceId;
    std::strncpy(mapping.filePath, filePath, sizeof(mapping.filePath) - 1);
    mapping.filePath[sizeof(mapping.filePath) - 1] = '\0';
    sourcePaths_.push_back(mapping);
}

void ButlerThreadImpl::unregisterSourcePath(uint32_t sourceId) {
    if (sourceId == 0) return;
    std::lock_guard<std::mutex> lock(sourcePathsMutex_);
    auto it = std::remove_if(sourcePaths_.begin(), sourcePaths_.end(),
        [sourceId](const SourcePathMapping& m) { return m.sourceId == sourceId; });
    sourcePaths_.erase(it, sourcePaths_.end());
}

void ButlerThreadImpl::updateTransportState(uint64_t positionSample, float sampleRate, bool isPlaying) {
    transportPos_.store(positionSample, std::memory_order_release);
    sampleRate_.store(sampleRate, std::memory_order_release);
    isPlaying_.store(isPlaying, std::memory_order_release);
}

void ButlerThreadImpl::setSampleRate(float sampleRate) {
    sampleRate_.store(sampleRate, std::memory_order_release);
    std::lock_guard<std::mutex> lock(bufferMutex);
    for (auto* buf : registeredBuffers) {
        if (buf) {
            buf->setSampleRate(static_cast<uint32_t>(sampleRate));
        }
    }
}

void ButlerThreadImpl::updateTimelineSnapshot(const TimelineSnapshot* snapshot) {
    activeSnapshot_.store(snapshot, std::memory_order_release);
}

void ButlerThreadImpl::registerBufferForRegion(uint64_t regionId, uint32_t sourceId, IStreamingBuffer* buffer) {
    if (regionId == 0) return;
    for (size_t i = 0; i < MAX_REGION_MAPPINGS; ++i) {
        uint64_t expected = 0;
        if (regionMappings_[i].regionId.compare_exchange_strong(expected, regionId, std::memory_order_acq_rel)) {
            regionMappings_[i].sourceId.store(sourceId, std::memory_order_release);
            regionMappings_[i].buffer.store(buffer, std::memory_order_release);
            return;
        }
        if (expected == regionId) {
            regionMappings_[i].sourceId.store(sourceId, std::memory_order_release);
            regionMappings_[i].buffer.store(buffer, std::memory_order_release);
            return;
        }
    }
}

void ButlerThreadImpl::unregisterBufferForRegion(uint64_t regionId) {
    if (regionId == 0) return;
    for (size_t i = 0; i < MAX_REGION_MAPPINGS; ++i) {
        if (regionMappings_[i].regionId.load(std::memory_order_acquire) == regionId) {
            regionMappings_[i].buffer.store(nullptr, std::memory_order_release);
            regionMappings_[i].sourceId.store(0, std::memory_order_release);
            regionMappings_[i].regionId.store(0, std::memory_order_release);
            return;
        }
    }
}

IStreamingBuffer* ButlerThreadImpl::getBufferForRegion(uint64_t regionId, [[maybe_unused]] uint32_t sourceId) const {
    if (regionId == 0) return nullptr;
    for (size_t i = 0; i < MAX_REGION_MAPPINGS; ++i) {
        if (regionMappings_[i].regionId.load(std::memory_order_acquire) == regionId) {
            return regionMappings_[i].buffer.load(std::memory_order_acquire);
        }
    }
    return nullptr;
}

std::unique_ptr<IButlerThread> IButlerThread::create() {
    return std::make_unique<ButlerThreadImpl>();
}

} // namespace Layer3
