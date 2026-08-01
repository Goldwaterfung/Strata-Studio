#pragma once

#include <memory>
#include <cstdint>
#include "Core infrastructure/bridges/spsc_queue.h"
#include "Media management/codecs/icodec_writer.h"

namespace MediaManagement {

// The queue size 524288 provides approx 10.9 seconds of buffering at 48kHz.
using RecordingQueue = Layer2::SPSCQueue<float, 524288>;

/**
 * @brief Interface for the background disk writer service.
 * Handles lock-free streaming of PCM data to disk during recording.
 */
class IDiskWriterService {
public:
    virtual ~IDiskWriterService() = default;

    /**
     * @brief Registers an armed track and allocates its lock-free recording queue.
     * @param trackId The ID of the track to record.
     * @param writer The codec writer instance for this track.
     * @return A shared pointer to the pre-allocated lock-free SPSC queue.
     */
    virtual std::pair<std::shared_ptr<RecordingQueue>, std::shared_ptr<Layer2::SPSCQueue<float, 16384>>> registerTrack(uint32_t trackId, std::shared_ptr<ICodecWriter> writer, uint32_t numChannels) = 0;

    /**
     * @brief Unregisters a track and closes its writer.
     * @param trackId The ID of the track to unregister.
     */
    virtual void unregisterTrack(uint32_t trackId) = 0;

    /**
     * @brief Starts the background disk writer thread.
     */
    virtual void start() = 0;

    /**
     * @brief Stops the background disk writer thread and performs a final flush.
     */
    virtual void stop() = 0;
};

} // namespace MediaManagement
