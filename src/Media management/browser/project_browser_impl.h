#pragma once

#include "iproject_browser.h"
#include "Media management/registry/imedia_registry.h"
#include "Core infrastructure/memory/istring_registry.h"
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <unordered_map>

namespace MediaManagement {

class IMediaIntakePipeline;

class ProjectBrowserImpl : public IProjectBrowser {
public:
    ProjectBrowserImpl(IMediaRegistry* registry, 
                       Layer2::IStringRegistry* strings,
                       ICodecFactory* codecs,
                       IAudioAnalysisEngine* analysis,
                       IWaveformRenderer* waveforms);
    ~ProjectBrowserImpl() override;

    uint64_t importAssetAsync(const ImportJob& job,
                                      ImportCallback callback,
                                      void* context) override;

    bool getImportProgress(uint64_t jobId, float& outProgress, const char*& outErrorMessage) const override;
    bool cancelImport(uint64_t jobId) override;
    void pruneJobs() override;
    
    uint32_t getAssetCount() const override;
    bool getAssetInfo(MediaID mediaId, AssetInfo& outInfo) const override;
    uint32_t findAssetsByName(uint32_t nameStringId, MediaID* outResults, uint32_t maxResults) const override;
    bool removeAsset(MediaID mediaId) override;

    void update() override;

private:
    struct InternalJob {
        uint64_t jobId;
        uint32_t filePathId;
        ImportOptions options;
        ImportCallback callback;
        void* context;
        
        ImportStatus status;
        float progress;
        char errorMessage[128];
        
        std::atomic<bool> cancelled{false};
        MediaID resultId;
    };

    void workerLoop();
    void processJob(InternalJob& job);

    IMediaRegistry* registry_;
    Layer2::IStringRegistry* strings_;
    ICodecFactory* codecs_;
    IAudioAnalysisEngine* analysis_;
    IWaveformRenderer* waveforms_;
    std::unique_ptr<IMediaIntakePipeline> intakePipeline_;
    
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<InternalJob>> jobs_;
    std::queue<uint64_t> pendingQueue_;
    
    struct CompletedJob {
        uint64_t jobId;
        MediaID result;
        bool success;
        std::string error;
        ImportCallback callback;
        void* context;
    };
    std::mutex completedMutex_;
    std::vector<CompletedJob> completedJobs_;

    std::thread workerThread_;
    std::atomic<bool> running_{true};
    std::condition_variable cv_;
    
    std::atomic<uint64_t> nextJobId_{1};
};

} // namespace MediaManagement
