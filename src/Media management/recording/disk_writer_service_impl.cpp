#include "Media management/recording/disk_writer_service_impl.h"
#include <chrono>

namespace MediaManagement {

DiskWriterServiceImpl::DiskWriterServiceImpl() {
    for (uint32_t i = 0; i < BATCH_SIZE; ++i) {
        m_processBuffer[i] = 0.0f;
    }
}

DiskWriterServiceImpl::~DiskWriterServiceImpl() {
    stop();
}

std::pair<std::shared_ptr<RecordingQueue>, std::shared_ptr<Layer2::SPSCQueue<float, 16384>>> DiskWriterServiceImpl::registerTrack(uint32_t trackId, std::shared_ptr<ICodecWriter> writer, uint32_t numChannels) {
    std::lock_guard<std::mutex> lock(m_tracksMutex);
    
    auto context = std::make_shared<TrackContext>();
    context->queue = std::make_shared<RecordingQueue>();
    context->peakQueue = std::make_shared<Layer2::SPSCQueue<float, 16384>>();
    context->writer = writer;
    context->numChannels = numChannels;
    context->active.store(true, std::memory_order_relaxed);

    m_tracks[trackId] = context;
    
    return {context->queue, context->peakQueue};
}

void DiskWriterServiceImpl::unregisterTrack(uint32_t trackId) {
    std::shared_ptr<TrackContext> ctxToClose;
    {
        std::lock_guard<std::mutex> lock(m_tracksMutex);
        auto it = m_tracks.find(trackId);
        if (it != m_tracks.end()) {
            it->second->active.store(false, std::memory_order_release);
            ctxToClose = it->second;
            m_tracks.erase(it);
        }
    }
    
    if (ctxToClose) {
        // Sleep briefly to ensure worker thread exits the write loop for this track
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (ctxToClose->writer && ctxToClose->writer->isValid()) {
            ctxToClose->writer->close();
        }
    }
}

void DiskWriterServiceImpl::start() {
    if (m_isRunning.exchange(true)) {
        return; // Already running
    }
    
    m_workerThread = std::thread(&DiskWriterServiceImpl::workerThreadFunc, this);
}

void DiskWriterServiceImpl::stop() {
    if (!m_isRunning.exchange(false)) {
        return;
    }

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    // Final flush of all queues after thread has stopped
    std::lock_guard<std::mutex> lock(m_tracksMutex);
    for (auto& pair : m_tracks) {
        auto& context = pair.second;
        if (context->queue && context->writer && context->writer->isValid()) {
            uint32_t popped = 0;
            while ((popped = context->queue->popMultiple(m_processBuffer, BATCH_SIZE)) > 0) {
                uint32_t floatsToPop = (popped / context->numChannels) * context->numChannels;
                context->writer->writeFrames(m_processBuffer, floatsToPop / context->numChannels);
            }
            context->writer->close();
        }
    }
}

void DiskWriterServiceImpl::workerThreadFunc() {
    while (m_isRunning.load(std::memory_order_relaxed)) {
        bool processedAny = false;
        
        std::vector<std::shared_ptr<TrackContext>> currentTracks;
        {
            std::lock_guard<std::mutex> lock(m_tracksMutex);
            for (auto& pair : m_tracks) {
                currentTracks.push_back(pair.second);
            }
        }
        
        for (auto& context : currentTracks) {
            if (!context->active.load(std::memory_order_acquire)) {
                continue;
            }
            if (!context->queue || !context->writer || !context->writer->isValid()) {
                continue;
            }

            uint32_t available = context->queue->availableRead();
            if (available == 0) continue;
            
            uint32_t floatsToPop = (available / context->numChannels) * context->numChannels;
            if (floatsToPop > BATCH_SIZE) floatsToPop = (BATCH_SIZE / context->numChannels) * context->numChannels;
            
            if (floatsToPop == 0) continue;

            uint32_t popped = context->queue->popMultiple(m_processBuffer, floatsToPop);
            if (popped > 0) {
                uint32_t frames = popped / context->numChannels;
                context->writer->writeFrames(m_processBuffer, frames);
                processedAny = true;
                
                if (context->peakQueue) {
                    for (uint32_t i = 0; i < frames; i += 64) {
                        float maxPeak = 0.0f;
                        uint32_t framesToScan = std::min<uint32_t>(64, frames - i);
                        for (uint32_t j = 0; j < framesToScan; ++j) {
                            for (uint32_t c = 0; c < context->numChannels; ++c) {
                                float val = std::abs(m_processBuffer[(i + j) * context->numChannels + c]);
                                if (val > maxPeak) maxPeak = val;
                            }
                        }
                        context->peakQueue->push(maxPeak);
                    }
                }
            }
        }
        
        if (!processedAny) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

} // namespace MediaManagement
