#pragma once

#include "Media management/recording/idisk_writer_service.h"
#include <thread>
#include <atomic>
#include <unordered_map>
#include <mutex>

namespace MediaManagement {

class DiskWriterServiceImpl : public IDiskWriterService {
public:
    DiskWriterServiceImpl();
    ~DiskWriterServiceImpl() override;

    std::pair<std::shared_ptr<RecordingQueue>, std::shared_ptr<Layer2::SPSCQueue<float, 16384>>> registerTrack(uint32_t trackId, std::shared_ptr<ICodecWriter> writer, uint32_t numChannels) override;
    void unregisterTrack(uint32_t trackId) override;

    void start() override;
    void stop() override;

private:
    struct TrackContext {
        std::shared_ptr<RecordingQueue> queue;
        std::shared_ptr<Layer2::SPSCQueue<float, 16384>> peakQueue;
        std::shared_ptr<ICodecWriter> writer;
        uint32_t numChannels;
        std::atomic<bool> active{true};
    };

    void workerThreadFunc();

    std::unordered_map<uint32_t, std::shared_ptr<TrackContext>> m_tracks;
    std::mutex m_tracksMutex;

    std::atomic<bool> m_isRunning{false};
    std::thread m_workerThread;

    // A pre-allocated buffer for reading from the lock-free queues and writing to the codec.
    static constexpr uint32_t BATCH_SIZE = 4096;
    float m_processBuffer[BATCH_SIZE];
};

} // namespace MediaManagement
