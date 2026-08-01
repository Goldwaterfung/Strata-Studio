// src/Core audio engine/streaming/streaming_buffer_impl.h
#pragma once

#include "ibutler_thread.h"
#include <atomic>
#include <vector>
#include <sndfile.h>

namespace RubberBand { class RubberBandStretcher; }

struct SRC_STATE_tag;
typedef struct SRC_STATE_tag SRC_STATE;

namespace Layer3 {

class StreamingBufferImpl : public IStreamingBuffer {
public:
    StreamingBufferImpl(uint32_t channels, uint32_t sampleRate);
    ~StreamingBufferImpl() override;

    // IStreamingBuffer implementation
    const float* const* getRTBuffer(uint64_t readPosition) override;
    void requestRefill(uint64_t readPosition) override;
    void provideRecordedData(const float* data, uint32_t numSamples) override;

    void associateFile(Layer1::FileHandle handle) override;
    void setTimelineOffset(uint64_t timelineOffset, uint64_t sourceStart) override;
    void refillAsync(uint64_t readPosition, Layer1::IFileSystem* fs) override;
    void flushAsync() override;
    void setPlaybackRatio(float ratio) override;

    BufferState getState() const override { return state.load(std::memory_order_relaxed); }
    uint64_t getReadPosition() const override { return readPos.load(std::memory_order_relaxed); }
    uint32_t getAvailableFrames() const override;
    uint32_t getTotalCapacity() const override { return capacity; }
    uint32_t getNumChannels() const override { return numChannels; }

    void setBufferSize(uint32_t numFrames) override;
    void setReadAheadSize(uint32_t numFrames) override;
    void setSampleRate(uint32_t newRate) override;
    uint32_t getSampleRate() const override { return sampleRate; }

private:
    uint32_t numChannels;
    uint32_t capacity;
    uint32_t readAheadThreshold;

    float** planarBuffer;
    
    std::atomic<uint64_t> writePos; // Producer (Butler)
    std::atomic<uint64_t> readPos;  // Consumer (Audio)
    std::atomic<BufferState> state;
    std::atomic<bool> refillRequested;

    uint64_t sourceStartSample_;
    Layer1::FileHandle fileHandle;
    uint64_t currentFileOffset; // Current frame offset
    uint64_t totalFrames;
    uint32_t fileChannels;
    std::atomic<float> playbackRatio;
    RubberBand::RubberBandStretcher* stretcher;
    float currentStretcherRatio;
    uint32_t sampleRate;

    SNDFILE* sndFileHandle;
    SF_INFO sfInfo;
    SRC_STATE* srcState;
    double lastSrcRatio;
    
    // Virtual IO state
    Layer1::IFileSystem* currentFs;
    uint64_t virtualFileOffset;

    static sf_count_t vio_get_filelen(void *user_data);
    static sf_count_t vio_seek(sf_count_t offset, int whence, void *user_data);
    static sf_count_t vio_read(void *ptr, sf_count_t count, void *user_data);
    static sf_count_t vio_write(const void *ptr, sf_count_t count, void *user_data);
    static sf_count_t vio_tell(void *user_data);

    void allocateBuffer(uint32_t numFrames);
    void deallocateBuffer();
    uint32_t readResampledInterleaved(float* outputBuffer, uint32_t requestedFrames);
};

} // namespace Layer3
