// src/Core audio engine/streaming/streaming_buffer_impl.cpp
#include "streaming_buffer_impl.h"
#include "Core infrastructure/memory/aligned_allocator.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <thread>
#include <chrono>
#include "Hardware/OS abstraction/filesystem/ifile_system.h"
#include <rubberband/RubberBandStretcher.h>
#include <samplerate.h>

namespace Layer3 {

using namespace Layer2;

sf_count_t StreamingBufferImpl::vio_get_filelen(void *user_data) {
    auto* ctx = static_cast<StreamingBufferImpl*>(user_data);
    if (!ctx->currentFs || ctx->fileHandle == Layer1::INVALID_FILE_HANDLE) return -1;
    return static_cast<sf_count_t>(ctx->currentFs->getFileSize(ctx->fileHandle));
}

sf_count_t StreamingBufferImpl::vio_seek(sf_count_t offset, int whence, void *user_data) {
    auto* ctx = static_cast<StreamingBufferImpl*>(user_data);
    if (!ctx->currentFs || ctx->fileHandle == Layer1::INVALID_FILE_HANDLE) return -1;
    
    uint64_t fileLen = ctx->currentFs->getFileSize(ctx->fileHandle);
    sf_count_t newOffset = 0;
    
    if (whence == SEEK_SET) {
        newOffset = offset;
    } else if (whence == SEEK_CUR) {
        newOffset = static_cast<sf_count_t>(ctx->virtualFileOffset) + offset;
    } else if (whence == SEEK_END) {
        newOffset = static_cast<sf_count_t>(fileLen) + offset;
    }
    
    if (newOffset < 0) newOffset = 0;
    if (static_cast<uint64_t>(newOffset) > fileLen) newOffset = static_cast<sf_count_t>(fileLen);
    
    ctx->virtualFileOffset = static_cast<uint64_t>(newOffset);
    return newOffset;
}

sf_count_t StreamingBufferImpl::vio_read(void *ptr, sf_count_t count, void *user_data) {
    auto* ctx = static_cast<StreamingBufferImpl*>(user_data);
    if (!ctx->currentFs || ctx->fileHandle == Layer1::INVALID_FILE_HANDLE) return 0;
    
    uint64_t read = ctx->currentFs->readFileSync(ctx->fileHandle, ctx->virtualFileOffset, static_cast<uint8_t*>(ptr), static_cast<uint64_t>(count));
    ctx->virtualFileOffset += read;
    return static_cast<sf_count_t>(read);
}

sf_count_t StreamingBufferImpl::vio_write(const void *ptr, sf_count_t count, void *user_data) {
    (void)ptr; (void)count; (void)user_data; return 0; // Read-only
}

sf_count_t StreamingBufferImpl::vio_tell(void *user_data) {
    auto* ctx = static_cast<StreamingBufferImpl*>(user_data);
    return static_cast<sf_count_t>(ctx->virtualFileOffset);
}


StreamingBufferImpl::StreamingBufferImpl(uint32_t channels, uint32_t sampleRate)
    : numChannels(channels)
    , capacity(0)
    , readAheadThreshold(0)
    , planarBuffer(nullptr)
    , writePos(0)
    , readPos(0)
    , state(BufferState::EMPTY)
    , refillRequested(false)
    , fileHandle(Layer1::INVALID_FILE_HANDLE)
    , currentFileOffset(0)
    , totalFrames(0)
    , fileChannels(channels)
    , playbackRatio(1.0f)
    , stretcher(nullptr)
    , currentStretcherRatio(1.0f)
    , sampleRate(sampleRate)
    , sndFileHandle(nullptr)
    , sfInfo{}
    , srcState(nullptr)
    , lastSrcRatio(0.0)
    , currentFs(nullptr)
    , virtualFileOffset(0)
{
    // Default capacity: 2 seconds of audio
    setBufferSize(sampleRate * 2);
    setReadAheadSize(sampleRate / 2); // 0.5s read-ahead
}

StreamingBufferImpl::~StreamingBufferImpl() {
    deallocateBuffer();
    if (stretcher) {
        delete stretcher;
        stretcher = nullptr;
    }
    if (sndFileHandle) {
        sf_close(sndFileHandle);
        sndFileHandle = nullptr;
    }
    if (srcState) {
        src_delete(srcState);
        srcState = nullptr;
    }
}

const float* const* StreamingBufferImpl::getRTBuffer(uint64_t currentReadPos) {
    uint64_t oldReadPos = readPos.load(std::memory_order_acquire);
    uint64_t wPos = writePos.load(std::memory_order_acquire);

    // Detect seek/discontinuity
    if (currentReadPos < oldReadPos || currentReadPos > wPos) {
        // Reset readPos to currentReadPos and state to EMPTY/DRAINING until refill completes
        readPos.store(currentReadPos, std::memory_order_release);
        state.store(BufferState::EMPTY, std::memory_order_relaxed);
        
        // requestRefill(currentReadPos) will trigger asynchronous refill
        requestRefill(currentReadPos);
        return nullptr;
    }

    // Update local read position
    readPos.store(currentReadPos, std::memory_order_release);

    if (wPos <= currentReadPos) {
        state.store(BufferState::EMPTY, std::memory_order_relaxed);
        return nullptr;
    }

    state.store(BufferState::DRAINING, std::memory_order_relaxed);

    // Calculate offset in circular buffer for all channels
    // NOTE: The caller must ensure their read size doesn't wrap within the buffer.
    // For typical process blocks (256-2048 samples) and large buffers (2+ seconds),
    // this is acceptable. The buffer is designed to be large enough that the
    // audio thread never needs to handle wrap within a single process cycle.
    uint32_t offset = static_cast<uint32_t>(currentReadPos % capacity);

    // Build planar pointer array for this read cycle
    // We use a pre-allocated static array to avoid allocation in RT path
    // This is safe because the audio thread is the only caller of this method
    static thread_local float* channelPtrs[64]; // Support up to 64 channels
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        channelPtrs[ch] = &planarBuffer[ch][offset];
    }

    return channelPtrs;
}

void StreamingBufferImpl::requestRefill(uint64_t currentReadPos) {
    uint64_t wPos = writePos.load(std::memory_order_acquire);

    // Detect seek/discontinuity in refill check
    if (currentReadPos > wPos || (wPos > currentReadPos && wPos - currentReadPos > capacity)) {
        refillRequested.store(true, std::memory_order_release);
        return;
    }

    uint32_t available = static_cast<uint32_t>(wPos - currentReadPos);

    if (available < readAheadThreshold) {
        refillRequested.store(true, std::memory_order_release);
    }
}

void StreamingBufferImpl::provideRecordedData(const float* data, uint32_t numSamples) {
    // Basic implementation for recording (pushing into buffer)
    uint64_t wPos = writePos.load(std::memory_order_relaxed);
    
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        for (uint32_t i = 0; i < numSamples; ++i) {
            planarBuffer[ch][(wPos + i) % capacity] = data[ch * numSamples + i];
        }
    }
    
    writePos.fetch_add(numSamples, std::memory_order_release);
}

void StreamingBufferImpl::associateFile(Layer1::FileHandle handle) {
    if (sndFileHandle) {
        sf_close(sndFileHandle);
        sndFileHandle = nullptr;
    }
    if (srcState) {
        src_delete(srcState);
        srcState = nullptr;
    }
    lastSrcRatio = 0.0;
    fileHandle = handle;
    currentFileOffset = 0;
    totalFrames = 0;
    fileChannels = numChannels;
    virtualFileOffset = 0;
}

void StreamingBufferImpl::setTimelineOffset(uint64_t timelineOffset, uint64_t sourceStart) {
    sourceStartSample_ = sourceStart;
    float ratio = playbackRatio.load(std::memory_order_relaxed);
    if (ratio <= 0.0f) ratio = 1.0f;
    
    double srRatio = 1.0;
    if (sndFileHandle) {
        srRatio = static_cast<double>(sfInfo.samplerate) / static_cast<double>(sampleRate);
    }
    currentFileOffset = sourceStart + static_cast<uint64_t>(static_cast<double>(timelineOffset) * static_cast<double>(ratio) * srRatio);

    if (stretcher) {
        stretcher->reset();
    }
    if (srcState) {
        src_reset(srcState);
    }
    if (sndFileHandle) {
        sf_seek(sndFileHandle, static_cast<sf_count_t>(currentFileOffset), SEEK_SET);
    }
    
    // Reset ring buffer positions to match the file offset
    // This ensures that the audio thread's RT playhead requests match the written data exactly
    std::atomic_thread_fence(std::memory_order_seq_cst);
    writePos.store(timelineOffset, std::memory_order_release);
    readPos.store(timelineOffset, std::memory_order_release);
    state.store(BufferState::EMPTY, std::memory_order_relaxed);
}

void StreamingBufferImpl::setPlaybackRatio(float ratio) {
    if (std::abs(playbackRatio.load(std::memory_order_relaxed) - ratio) > 0.001f) {
        playbackRatio.store(ratio, std::memory_order_release);
        flushAsync();
    }
}

void StreamingBufferImpl::refillAsync(uint64_t currentReadPos, Layer1::IFileSystem* fs) {
    if (!fs || fileHandle == Layer1::INVALID_FILE_HANDLE) {
        return;
    }

    if (!refillRequested.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    state.store(BufferState::FILLING, std::memory_order_relaxed);

    currentFs = fs;

    float ratio = playbackRatio.load(std::memory_order_acquire);
    
    // Manage stretcher lifecycle
    if (std::abs(ratio - 1.0f) < 0.001f) {
        if (stretcher) {
            delete stretcher;
            stretcher = nullptr;
        }
        currentStretcherRatio = 1.0f;
    } else {
        if (!stretcher || std::abs(ratio - currentStretcherRatio) > 0.001f) {
            if (stretcher) delete stretcher;
            
            int options = RubberBand::RubberBandStretcher::OptionProcessRealTime | 
                          RubberBand::RubberBandStretcher::OptionEngineFiner |
                          RubberBand::RubberBandStretcher::OptionTransientsCrisp;
            stretcher = new RubberBand::RubberBandStretcher(sampleRate, numChannels, options);
            stretcher->setTimeRatio(1.0 / static_cast<double>(ratio));
            currentStretcherRatio = ratio;
        }
    }

    // 1. Parse WAV Header if not already done
    if (!sndFileHandle) {
        SF_VIRTUAL_IO vio;
        vio.get_filelen = vio_get_filelen;
        vio.seek = vio_seek;
        vio.read = vio_read;
        vio.write = vio_write;
        vio.tell = vio_tell;

        virtualFileOffset = 0;
        
        std::memset(&sfInfo, 0, sizeof(sfInfo));
        sndFileHandle = sf_open_virtual(&vio, SFM_READ, &sfInfo, this);
        
        if (!sndFileHandle) {
            state.store(BufferState::EMPTY, std::memory_order_relaxed);
            return;
        }
        
        totalFrames = static_cast<uint64_t>(sfInfo.frames);
        fileChannels = static_cast<uint32_t>(sfInfo.channels);
        if (fileChannels == 0) fileChannels = numChannels;
        
        double srRatio = static_cast<double>(sfInfo.samplerate) / static_cast<double>(sampleRate);
        float currentRatio = stretcher ? (currentStretcherRatio > 0.0f ? currentStretcherRatio : 1.0f) : ratio;
        if (currentRatio <= 0.0f) currentRatio = 1.0f;
        uint64_t initialWPos = writePos.load(std::memory_order_relaxed);
        currentFileOffset = sourceStartSample_ + static_cast<uint64_t>(static_cast<double>(initialWPos) * static_cast<double>(currentRatio) * srRatio);
        
        sf_seek(sndFileHandle, static_cast<sf_count_t>(currentFileOffset), SEEK_SET);
    }

    // 2. Read audio data
    uint64_t wPos = writePos.load(std::memory_order_relaxed);
    
    // Detect seek/discontinuity and re-anchor writePos
    if (currentReadPos > wPos || (wPos > currentReadPos && wPos - currentReadPos > capacity)) {
        wPos = currentReadPos;
        writePos.store(wPos, std::memory_order_release);
        
        double srRatio = static_cast<double>(sfInfo.samplerate) / static_cast<double>(sampleRate);
        if (stretcher) {
            stretcher->reset();
            float currentRatio = currentStretcherRatio > 0.0f ? currentStretcherRatio : 1.0f;
            currentFileOffset = sourceStartSample_ + static_cast<uint64_t>(static_cast<double>(wPos) * static_cast<double>(currentRatio) * srRatio);
        } else {
            currentFileOffset = sourceStartSample_ + static_cast<uint64_t>(static_cast<double>(wPos) * srRatio);
        }
        sf_seek(sndFileHandle, static_cast<sf_count_t>(currentFileOffset), SEEK_SET);
    }

    if (stretcher) {
        uint32_t fillAmount = 2048; // Micro-chunked to prevent CPU starvation
        uint32_t samplesWritten = 0;
        uint64_t maxSafeWritePos = currentReadPos + capacity;

        while (samplesWritten < fillAmount && wPos + samplesWritten < maxSafeWritePos) {
            int avail = stretcher->available();
            if (avail > 0) {
                uint32_t toWrite = std::min(static_cast<uint32_t>(avail), fillAmount - samplesWritten);
                uint64_t safeSpace = maxSafeWritePos - (wPos + samplesWritten);
                toWrite = std::min(toWrite, static_cast<uint32_t>(safeSpace));
                if (toWrite == 0) break;

                uint32_t chunkStart = (wPos + samplesWritten) % capacity;
                uint32_t chunkLen = std::min(toWrite, capacity - chunkStart);

                float* chunkPtrs[64];
                for (uint32_t ch = 0; ch < numChannels; ++ch) {
                    chunkPtrs[ch] = &planarBuffer[ch][chunkStart];
                }

                stretcher->retrieve(chunkPtrs, chunkLen);
                samplesWritten += chunkLen;
                
                // Micro-sleep to yield back to OS scheduler, preventing E-Core thermal throttling
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                continue;
            }

            static thread_local std::vector<float> silentBuffer(512, 0.0f);
            float* dummyInput[64] = { nullptr };
            for (uint32_t ch = 0; ch < std::min<uint32_t>(numChannels, 64); ++ch) {
                dummyInput[ch] = silentBuffer.data();
            }

            if (currentFileOffset >= totalFrames) {
                stretcher->process(dummyInput, 0, true);
                int finalAvail = stretcher->available();
                if (finalAvail == 0) break;
                continue;
            }

            uint32_t readSize = 512;
            static thread_local std::vector<float> interleavedBuffer;
            uint32_t numItemsToRead = readSize * fileChannels;
            if (interleavedBuffer.size() < numItemsToRead) interleavedBuffer.resize(numItemsToRead);

            uint32_t samplesRead = readResampledInterleaved(interleavedBuffer.data(), readSize);

            if (samplesRead == 0) {
                stretcher->process(dummyInput, 0, true);
                int finalAvail = stretcher->available();
                if (finalAvail == 0) break;
                continue;
            }

            float tempPlanar[64][512];
            float* tempPlanarPtrs[64];
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                tempPlanarPtrs[ch] = tempPlanar[ch];
            }

            for (uint32_t i = 0; i < samplesRead; ++i) {
                if (fileChannels == 2 && numChannels == 1) {
                    float left = interleavedBuffer[i * 2 + 0];
                    float right = interleavedBuffer[i * 2 + 1];
                    tempPlanar[0][i] = 0.5f * (left + right);
                } else {
                    for (uint32_t ch = 0; ch < numChannels; ++ch) {
                        uint32_t srcCh = (ch < fileChannels) ? ch : 0;
                        tempPlanar[ch][i] = interleavedBuffer[i * fileChannels + srcCh];
                    }
                }
            }

            stretcher->process(tempPlanarPtrs, samplesRead, false);
        }

        if (samplesWritten > 0) {
            writePos.fetch_add(samplesWritten, std::memory_order_release);
            state.store(BufferState::READY, std::memory_order_relaxed);
        } else {
            state.store(BufferState::EMPTY, std::memory_order_relaxed);
        }
    } else {
        uint32_t fileSr = (sfInfo.samplerate > 0) ? static_cast<uint32_t>(sfInfo.samplerate) : sampleRate;
        double srRatio = static_cast<double>(sampleRate) / static_cast<double>(fileSr);
        uint64_t totalProjectFrames = static_cast<uint64_t>(static_cast<double>(totalFrames) * srRatio);

        if (wPos >= totalProjectFrames) {
            state.store(BufferState::EMPTY, std::memory_order_relaxed);
            return;
        }

        uint32_t fillAmount = capacity / 4;
        uint64_t maxSafeWritePos = currentReadPos + capacity;
        if (wPos + fillAmount > maxSafeWritePos) {
            fillAmount = (maxSafeWritePos > wPos) ? static_cast<uint32_t>(maxSafeWritePos - wPos) : 0;
        }

        if (wPos + fillAmount > totalProjectFrames) {
            fillAmount = (totalProjectFrames > wPos) ? static_cast<uint32_t>(totalProjectFrames - wPos) : 0;
        }
        
        if (fillAmount == 0) {
            state.store(BufferState::EMPTY, std::memory_order_relaxed);
            return;
        }
        
        static thread_local std::vector<float> interleavedBuffer;
        uint32_t numItemsToRead = fillAmount * fileChannels;
        if (interleavedBuffer.size() < numItemsToRead) interleavedBuffer.resize(numItemsToRead);
        
        uint32_t samplesRead = readResampledInterleaved(interleavedBuffer.data(), fillAmount);
        
        if (samplesRead > 0) {
            for (uint32_t i = 0; i < samplesRead; ++i) {
                uint32_t bufferIdx = static_cast<uint32_t>((wPos + i) % capacity);
                if (fileChannels == 2 && numChannels == 1) {
                    float left = interleavedBuffer[i * 2 + 0];
                    float right = interleavedBuffer[i * 2 + 1];
                    planarBuffer[0][bufferIdx] = 0.5f * (left + right);
                } else {
                    for (uint32_t ch = 0; ch < numChannels; ++ch) {
                        uint32_t srcCh = (ch < fileChannels) ? ch : 0;
                        float sample = interleavedBuffer[i * fileChannels + srcCh];
                        planarBuffer[ch][bufferIdx] = sample;
                    }
                }
            }
            
            writePos.fetch_add(samplesRead, std::memory_order_release);
            state.store(BufferState::READY, std::memory_order_relaxed);
        } else {
            state.store(BufferState::EMPTY, std::memory_order_relaxed);
        }
    }
}
void StreamingBufferImpl::flushAsync() {
    // Use a sequence fence to ensure all prior writes are visible before resetting
    std::atomic_thread_fence(std::memory_order_seq_cst);
    writePos.store(0, std::memory_order_release);
    readPos.store(0, std::memory_order_release);
    state.store(BufferState::EMPTY, std::memory_order_relaxed);
}

uint32_t StreamingBufferImpl::getAvailableFrames() const {
    uint64_t wPos = writePos.load(std::memory_order_acquire);
    uint64_t rPos = readPos.load(std::memory_order_acquire);
    if (wPos <= rPos) return 0;
    return static_cast<uint32_t>(wPos - rPos);
}

void StreamingBufferImpl::setBufferSize(uint32_t numFrames) {
    deallocateBuffer();
    allocateBuffer(numFrames);
}

void StreamingBufferImpl::setReadAheadSize(uint32_t numFrames) {
    readAheadThreshold = numFrames;
}

void StreamingBufferImpl::setSampleRate(uint32_t newRate) {
    if (newRate == 0 || sampleRate == newRate) return;
    sampleRate = newRate;
    if (srcState) {
        src_delete(srcState);
        srcState = nullptr;
    }
    lastSrcRatio = 0.0;
    if (stretcher) {
        delete stretcher;
        stretcher = nullptr;
        currentStretcherRatio = 1.0f;
    }
    flushAsync();
}

void StreamingBufferImpl::allocateBuffer(uint32_t numFrames) {
    capacity = numFrames;
    // Allocate pointer array using AlignedAllocator for consistency
    planarBuffer = static_cast<float**>(AlignedAllocator::allocate(
        numChannels * sizeof(float*), alignof(float*)));
    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        planarBuffer[ch] = static_cast<float*>(AlignedAllocator::allocate(capacity * sizeof(float), 64));
        std::memset(planarBuffer[ch], 0, capacity * sizeof(float));
    }
}

void StreamingBufferImpl::deallocateBuffer() {
    if (planarBuffer) {
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            AlignedAllocator::deallocate(planarBuffer[ch]);
        }
        AlignedAllocator::deallocate(planarBuffer);
        planarBuffer = nullptr;
    }
}

uint32_t StreamingBufferImpl::readResampledInterleaved(float* outputBuffer, uint32_t requestedFrames) {
    if (!sndFileHandle || requestedFrames == 0) return 0;

    double srcRatio = static_cast<double>(sampleRate) / static_cast<double>(sfInfo.samplerate);
    bool needsSrc = (std::abs(srcRatio - 1.0) > 0.0001);

    if (!needsSrc) {
        // Read directly
        sf_count_t read = sf_readf_float(sndFileHandle, outputBuffer, requestedFrames);
        if (read > 0) {
            currentFileOffset += static_cast<uint64_t>(read);
            return static_cast<uint32_t>(read);
        }
        return 0;
    }

    // Initialize or recreate srcState if needed
    if (!srcState || std::abs(srcRatio - lastSrcRatio) > 0.0001) {
        if (srcState) {
            src_delete(srcState);
        }
        int err = 0;
        srcState = src_new(SRC_SINC_FASTEST, static_cast<int>(fileChannels), &err);
        lastSrcRatio = srcRatio;
    }

    // Calculate input frames needed
    uint32_t inputFramesToRead = static_cast<uint32_t>(std::ceil(static_cast<double>(requestedFrames) / srcRatio)) + 16;
    if (currentFileOffset + inputFramesToRead > totalFrames) {
        inputFramesToRead = (totalFrames > currentFileOffset) ? static_cast<uint32_t>(totalFrames - currentFileOffset) : 0;
    }

    if (inputFramesToRead == 0) {
        // End of file, try to flush the resampler with zero input
        SRC_DATA srcData{};
        srcData.data_in = nullptr;
        srcData.data_out = outputBuffer;
        srcData.input_frames = 0;
        srcData.output_frames = requestedFrames;
        srcData.src_ratio = srcRatio;
        srcData.end_of_input = 1;

        src_process(srcState, &srcData);
        return static_cast<uint32_t>(srcData.output_frames_gen);
    }

    static thread_local std::vector<float> inputBuffer;
    uint32_t numItemsToRead = inputFramesToRead * fileChannels;
    if (inputBuffer.size() < numItemsToRead) {
        inputBuffer.resize(numItemsToRead);
    }

    sf_count_t actualFramesRead = sf_readf_float(sndFileHandle, inputBuffer.data(), inputFramesToRead);
    if (actualFramesRead <= 0) {
        SRC_DATA srcData{};
        srcData.data_in = nullptr;
        srcData.data_out = outputBuffer;
        srcData.input_frames = 0;
        srcData.output_frames = requestedFrames;
        srcData.src_ratio = srcRatio;
        srcData.end_of_input = 1;

        src_process(srcState, &srcData);
        return static_cast<uint32_t>(srcData.output_frames_gen);
    }

    SRC_DATA srcData{};
    srcData.data_in = inputBuffer.data();
    srcData.data_out = outputBuffer;
    srcData.input_frames = actualFramesRead;
    srcData.output_frames = requestedFrames;
    srcData.src_ratio = srcRatio;
    srcData.end_of_input = (currentFileOffset + static_cast<uint64_t>(actualFramesRead) >= totalFrames) ? 1 : 0;

    int err = src_process(srcState, &srcData);
    if (err != 0) {
        return 0;
    }

    // Update currentFileOffset based on what the resampler actually consumed
    currentFileOffset += static_cast<uint64_t>(srcData.input_frames_used);

    // Seek back if we read more than consumed
    if (srcData.input_frames_used < actualFramesRead) {
        sf_seek(sndFileHandle, srcData.input_frames_used - actualFramesRead, SEEK_CUR);
    }

    return static_cast<uint32_t>(srcData.output_frames_gen);
}

std::unique_ptr<IStreamingBuffer> IStreamingBuffer::create(uint32_t channels, uint32_t sampleRate) {
    return std::make_unique<StreamingBufferImpl>(channels, sampleRate);
}

} // namespace Layer3
