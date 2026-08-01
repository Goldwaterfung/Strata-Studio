#include <catch2/catch_test_macros.hpp>
#include "Core audio engine/streaming/ibutler_thread.h"
#include "Hardware/OS abstraction/filesystem/ifile_system.h"
#include "Hardware/OS abstraction/filesystem/codecs/wav_codec.h"
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <map>

using namespace Layer3;
using namespace Layer1;

class MockStreamingFileSystem : public IFileSystem {
public:
    std::map<FileHandle, uint64_t> fileSizes;
    std::atomic<int> readDelayMs{0};
    uint64_t nextHandle = 1;

    FileHandle openFile(const char* /*path*/, bool /*forReading*/ = true) override {
        FileHandle handle = nextHandle++;
        fileSizes[handle] = 10 * 1024 * 1024; // 10MB default
        return handle;
    }

    void closeFile(FileHandle handle) override {
        fileSizes.erase(handle);
    }

    uint64_t getFileSize(FileHandle handle) override {
        auto it = fileSizes.find(handle);
        if (it != fileSizes.end()) return it->second;
        return 0;
    }

    OperationHandle readFileAsync(FileHandle /*handle*/, uint64_t /*offset*/, uint64_t /*bytesToRead*/, IAsyncCallback* /*callback*/) override { return 0; }
    OperationHandle writeFileAsync(FileHandle /*handle*/, uint64_t /*offset*/, uint8_t* /*data*/, uint64_t /*bytesToWrite*/, IAsyncCallback* /*callback*/) override { return 0; }
    bool cancelOperation(OperationHandle /*op*/) override { return true; }
    bool setPriority(FileHandle /*handle*/, IOPriority /*priority*/) override { return true; }

    uint64_t readFileSync(FileHandle /*handle*/, uint64_t offset, uint8_t* buffer, uint64_t bytesToRead) override {
        if (readDelayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(readDelayMs.load()));
        }

        static uint8_t headerBuf[44] = {0};
        static bool headerInit = false;
        if (!headerInit) {
            WAVCodec::Header header = {};
            header.chunkID[0] = 'R'; header.chunkID[1] = 'I'; header.chunkID[2] = 'F'; header.chunkID[3] = 'F';
            header.format[0] = 'W'; header.format[1] = 'A'; header.format[2] = 'V'; header.format[3] = 'E';
            header.subchunk1ID[0] = 'f'; header.subchunk1ID[1] = 'm'; header.subchunk1ID[2] = 't'; header.subchunk1ID[3] = ' ';
            header.subchunk1Size = 16;
            header.audioFormat = 1; // PCM
            header.numChannels = 1;
            header.sampleRate = 48000;
            header.bitsPerSample = 16;
            header.byteRate = 48000 * 1 * 2;
            header.blockAlign = 1 * 2;
            header.subchunk2ID[0] = 'd'; header.subchunk2ID[1] = 'a'; header.subchunk2ID[2] = 't'; header.subchunk2ID[3] = 'a';
            header.subchunk2Size = 10 * 1024 * 1024; // 10MB data chunk
            header.dataOffset = 44;
            WAVCodec::writeHeader(headerBuf, header);
            headerInit = true;
        }

        uint64_t bytesRead = 0;
        if (offset < 44) {
            uint64_t toCopy = std::min(bytesToRead, 44 - offset);
            std::memcpy(buffer, headerBuf + offset, toCopy);
            bytesRead += toCopy;
            offset += toCopy;
            buffer += toCopy;
            bytesToRead -= toCopy;
        }
        
        if (bytesToRead > 0) {
            std::memset(buffer, 0, bytesToRead);
            bytesRead += bytesToRead;
        }

        return bytesRead;
    }

    uint64_t writeFileSync(FileHandle /*handle*/, uint64_t /*offset*/, const uint8_t* /*buffer*/, uint64_t /*bytesToWrite*/) override { return 0; }
    bool iterateDirectory(const char* /*path*/, const std::function<void(const FileInfo&)>& /*callback*/) override { return false; }
    bool getPathInfo(const char* /*path*/, FileInfo& /*outInfo*/) override { return false; }
    bool exists(const char* /*path*/) override { return true; }
};

TEST_CASE("Disk Streaming: Stress Test", "[Layer3][Streaming]") {
    auto fs = std::make_unique<MockStreamingFileSystem>();
    auto butler = IButlerThread::create();
    REQUIRE(butler != nullptr);
    
    butler->attachFileSystem(fs.get());
    butler->start();

    const int numTracks = 100;
    std::vector<std::unique_ptr<IStreamingBuffer>> buffers;
    
    struct ButlerCleanup {
        IButlerThread* butler;
        std::vector<std::unique_ptr<IStreamingBuffer>>* buffers;
        ~ButlerCleanup() {
            if (butler) {
                if (buffers) {
                    for (auto& buf : *buffers) {
                        if (buf) butler->unregisterBuffer(buf.get());
                    }
                }
                butler->stop();
            }
        }
    } cleanup{butler.get(), &buffers};
    
    SECTION("High-Track Count Loads (100+ mono tracks)") {
        for (int i = 0; i < numTracks; ++i) {
            auto buf = IStreamingBuffer::create(1, 48000); // Mono, 48kHz
            buf->setBufferSize(48000 * 10); // 10 seconds cushion
            buf->setReadAheadSize(48000 * 5); // 5 seconds read-ahead
            
            FileHandle handle = fs->openFile("fake_track.wav");
            buf->associateFile(handle);
            
            bool registered = butler->registerBuffer(buf.get());
            REQUIRE(registered == true);
            
            buffers.push_back(std::move(buf));
        }
        
        // Call requestRefill for all 100 buffers simultaneously; call wakeButler()
        for (auto& buf : buffers) {
            buf->requestRefill(0);
        }
        butler->wakeButler();
        
        // Assert all buffers reach READY within a larger safety window (up to 1000ms)
        bool allReady = false;
        for (int retry = 0; retry < 100; ++retry) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            int readyCount = 0;
            for (auto& buf : buffers) {
                if (buf->getState() == IStreamingBuffer::BufferState::READY) {
                    readyCount++;
                }
            }
            if (readyCount == numTracks) {
                allReady = true;
                break;
            }
        }
        REQUIRE(allReady == true);
        
        // Simulate 1000 audio cycles; in each cycle, call getRTBuffer for all 100 tracks. Assert no nullptr returns.
        uint64_t currentReadPos = 0;
        uint32_t blockSize = 512;
        bool anyNullptr = false;
        
        for (int cycle = 0; cycle < 1000; ++cycle) {
            for (auto& buf : buffers) {
                const float* const* ptrs = buf->getRTBuffer(currentReadPos);
                if (ptrs == nullptr) {
                    anyNullptr = true;
                }
                buf->requestRefill(currentReadPos);
            }
            butler->wakeButler();
            currentReadPos += blockSize;
            
            // Give butler some time to process to avoid outrunning it in this fast simulation
            if (cycle % 2 == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        REQUIRE(anyNullptr == false);
        
        // Simulate a disk-slowdown (inject 10ms delay in IFileSystem mock)
        fs->readDelayMs.store(10);
        
        // Consume a large block to force refill (must be > read-ahead threshold of 240,000)
        currentReadPos += 250000;
        for (auto& buf : buffers) {
            buf->getRTBuffer(currentReadPos);
            buf->requestRefill(currentReadPos);
        }
        butler->wakeButler();
        
        // Verify State correctly moves to FILLING
        bool anyFilling = false;
        for (int retry = 0; retry < 10; ++retry) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            for (auto& buf : buffers) {
                if (buf->getState() == IStreamingBuffer::BufferState::FILLING) {
                    anyFilling = true;
                    break;
                }
            }
            if (anyFilling) break;
        }
        CHECK(anyFilling == true);

        // Verify Telemetry: Pending buffer count
        uint32_t pending = butler->getPendingBufferCount();
        CHECK(pending > 0); // Should have pending refills due to fs delay
        
        // Clean up
        for (auto& buf : buffers) {
            butler->unregisterBuffer(buf.get());
        }
        
        butler->stop();
    }
}

TEST_CASE("Disk Streaming: Butler Look-Ahead Dynamic Pre-fetching", "[Layer3][Streaming][Lookahead]") {
    auto fs = std::make_unique<MockStreamingFileSystem>();
    auto butler = IButlerThread::create();
    REQUIRE(butler != nullptr);

    butler->attachFileSystem(fs.get());
    butler->start();

    // 1. Create a track buffer and register it for track ID 1
    auto buf = IStreamingBuffer::create(1, 48000); // Mono, 48kHz
    buf->setBufferSize(48000 * 2);
    buf->setReadAheadSize(48000 / 2);

    butler->registerBuffer(buf.get());
    butler->registerBufferForTrack(1, buf.get());

    // Register a source path mapping: sourceId 10 -> "clip10.wav"
    butler->registerSourcePath(10, "clip10.wav");

    // 2. Set up TimelineSnapshot with a region inside the look-ahead window
    // Window: [0, 96000] (2 seconds at 48kHz)
    TimelineSnapshot snapshot{};
    snapshot.regionCount = 1;
    snapshot.regions[0].trackId = TrackID{1, 1};
    snapshot.regions[0].regionId = RegionID{5, 1};
    snapshot.regions[0].type = RegionType::AUDIO;
    snapshot.regions[0].sourceId = 10;
    snapshot.regions[0].positionSample = 24000; // starts in 0.5 seconds
    snapshot.regions[0].sourceStartSample = 0;
    snapshot.regions[0].durationProjectFrames = 48000 * 5; // 5 seconds long
    snapshot.regions[0].gain = 1.0f;
    snapshot.regions[0].isMuted = false;

    // Publish snapshot and update transport position to 0 (stopped, but we should pre-buffer at startup/seek)
    butler->updateTransportState(0, 48000.0f, false);
    butler->updateTimelineSnapshot(&snapshot);
    butler->wakeButler();

    // Give the butler thread some time to process
    bool prefetchSuccessful = false;
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (butler->getBufferForRegion(RegionID{5, 1}.toRaw(), 10) == buf.get() && buf->getState() == IStreamingBuffer::BufferState::READY) {
            prefetchSuccessful = true;
            break;
        }
    }
    REQUIRE(prefetchSuccessful == true);

    // 3. Verify eviction: move the playhead way past the region (e.g. sample 300000)
    // The region ends at 24000 + 240000 = 264000. So sample 300000 is past the region.
    butler->updateTransportState(300000, 48000.0f, true);
    butler->wakeButler();

    bool evictionSuccessful = false;
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (butler->getBufferForRegion(RegionID{5, 1}.toRaw(), 10) == nullptr) {
            evictionSuccessful = true;
            break;
        }
    }
    REQUIRE(evictionSuccessful == true);

    // Clean up
    butler->unregisterBufferForTrack(1);
    butler->unregisterBuffer(buf.get());
    butler->unregisterSourcePath(10);
    butler->stop();
}

TEST_CASE("Disk Streaming: Resampling (48kHz File to 44.1kHz Project)", "[Layer3][Streaming][Resampling]") {
    auto fs = std::make_unique<MockStreamingFileSystem>();
    auto butler = IButlerThread::create();
    REQUIRE(butler != nullptr);
    
    butler->attachFileSystem(fs.get());
    butler->start();

    // Create a buffer with project sample rate 44100 (while the mock file is 48000)
    auto buf = IStreamingBuffer::create(1, 44100); 
    buf->setBufferSize(44100 * 2);
    buf->setReadAheadSize(44100 / 2);

    FileHandle handle = fs->openFile("fake_track_48k.wav");
    buf->associateFile(handle);

    butler->registerBuffer(buf.get());
    
    // Trigger refill
    buf->requestRefill(0);
    butler->wakeButler();

    // Wait for READY state
    bool isReady = false;
    for (int i = 0; i < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (buf->getState() == IStreamingBuffer::BufferState::READY) {
            isReady = true;
            break;
        }
    }
    REQUIRE(isReady == true);

    // Verify we can read frames without issues
    const float* const* ptrs = buf->getRTBuffer(0);
    REQUIRE(ptrs != nullptr);
    REQUIRE(ptrs[0] != nullptr);

    butler->unregisterBuffer(buf.get());
    butler->stop();
}

