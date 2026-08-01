#pragma once

#include "iaudio_analysis_engine.h"
#include "Media management/registry/asset_blobs.h"
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <vector>
#include <unordered_map>

namespace Layer2 { class IStringRegistry; }

namespace MediaManagement {

class IMediaRegistry;
class ICodecFactory;

class AudioAnalysisEngineImpl : public IAudioAnalysisEngine {
public:
    explicit AudioAnalysisEngineImpl(class IMediaRegistry* registry, 
                                     class Layer2::IStringRegistry* strings,
                                     class ICodecFactory* codecFactory,
                                     uint32_t fftSize);
    ~AudioAnalysisEngineImpl() override;

    // --- IAudioAnalysisEngine Interface ---
    
    void analyzeRealtime(const struct AudioBuffer* input,
                         uint32_t numSamples,
                         AnalysisResult& outResult) override;

    bool analyze(MediaID mediaId, AnalysisResult& result) override;

    bool analyzeAsync(MediaID mediaId, 
                     CompletionCallback callback,
                     void* context) override;

    void getSpectralFluxData(MediaID mediaId, float* buffer, uint32_t bufferSize) override;
    void getTransientData(MediaID mediaId, uint64_t* positions, float* amplitudes, uint32_t bufferSize) override;
    void getPitchData(MediaID mediaId, float* buffer, uint32_t bufferSize) override;

    void calculateLoudness(MediaID mediaId, float* integrated, float* range, float* truePeak) override;
    void detectTempo(MediaID mediaId, float* tempo, float* confidence) override;
    void detectKey(MediaID mediaId, uint8_t* root, bool* isMinor, float* confidence) override;

    void update() override;

    std::unique_ptr<class IAnalysisSink> createSink(uint32_t sampleRate, uint16_t numChannels) override;

private:
    struct AnalysisJob {
        MediaID mediaId;
        CompletionCallback callback;
        void* context;
    };

    struct CompletedResult {
        AnalysisResult result;
        CompletionCallback callback;
        void* context;
    };


    void workerLoop();
    void processJob(const AnalysisJob& job);
    bool performAnalysis(MediaID mediaId, AnalysisResult& result, AssetAnalysisBlobs* largeData = nullptr);

    class IMediaRegistry* registry_;
    class Layer2::IStringRegistry* strings_;
    class ICodecFactory* codecFactory_;
    [[maybe_unused]] uint32_t fftSize_;

    mutable std::mutex mutex_;
    std::queue<AnalysisJob> jobQueue_;
    
    std::mutex resultsMutex_;
    std::vector<CompletedResult> completedResults_;
    
    // Pre-allocated for update() to avoid allocations on main thread
    std::vector<CompletedResult> updateSwapBuffer_;

    std::thread workerThread_;
    std::atomic<bool> running_{true};
    std::condition_variable cv_;
};

} // namespace MediaManagement
