#include "project_browser_impl.h"
#include "Media management/codecs/icodec_factory.h"
#include "Media management/codecs/icodec_reader.h"
#include "Media management/analysis/iaudio_analysis_engine.h"
#include "Media management/waveforms/iwaveform_renderer.h"
#include "Media management/intake/imedia_intake_pipeline.h"
#include <chrono>
#include <filesystem>
#include <cstring>

namespace MediaManagement {

ProjectBrowserImpl::ProjectBrowserImpl(IMediaRegistry* registry, 
                                       Layer2::IStringRegistry* strings,
                                       ICodecFactory* codecs,
                                       IAudioAnalysisEngine* analysis,
                                       IWaveformRenderer* waveforms)
    : registry_(registry), strings_(strings), codecs_(codecs), analysis_(analysis), waveforms_(waveforms)
{
    intakePipeline_ = IMediaIntakePipeline::create(registry, strings, codecs, analysis, waveforms);
    workerThread_ = std::thread(&ProjectBrowserImpl::workerLoop, this);
}

ProjectBrowserImpl::~ProjectBrowserImpl() {
    running_ = false;
    cv_.notify_all();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

uint64_t ProjectBrowserImpl::importAssetAsync(const ImportJob& job,
                                              ImportCallback callback,
                                              void* context) {
    std::unique_lock lock(mutex_);
    
    uint64_t jobId = job.jobId != 0 ? job.jobId : nextJobId_++;
    auto internalJob = std::make_shared<InternalJob>();
    internalJob->jobId = jobId;
    internalJob->filePathId = job.filePathId;
    internalJob->options = job.options;
    internalJob->callback = callback;
    internalJob->context = context;
    internalJob->status = ImportStatus::PENDING;
    internalJob->progress = 0.0f;
    internalJob->errorMessage[0] = '\0';
    internalJob->resultId = MediaID::invalid();
    
    jobs_[jobId] = internalJob;
    pendingQueue_.push(jobId);
    
    cv_.notify_one();
    return jobId;
}

bool ProjectBrowserImpl::getImportProgress(uint64_t jobId, float& outProgress, const char*& outErrorMessage) const {
    std::unique_lock lock(mutex_);
    auto it = jobs_.find(jobId);
    if (it != jobs_.end()) {
        outProgress = it->second->progress;
        outErrorMessage = it->second->errorMessage;
        return true;
    }
    return false;
}

bool ProjectBrowserImpl::cancelImport(uint64_t jobId) {
    std::unique_lock lock(mutex_);
    auto it = jobs_.find(jobId);
    if (it != jobs_.end()) {
        it->second->cancelled = true;
        return true;
    }
    return false;
}

void ProjectBrowserImpl::pruneJobs() {
    std::unique_lock lock(mutex_);
    for (auto it = jobs_.begin(); it != jobs_.end(); ) {
        if (it->second->status == ImportStatus::COMPLETE || 
            it->second->status == ImportStatus::FAILED) {
            it = jobs_.erase(it);
        } else {
            ++it;
        }
    }
}

uint32_t ProjectBrowserImpl::getAssetCount() const {
    return registry_->getAssetCount();
}

bool ProjectBrowserImpl::getAssetInfo(MediaID mediaId, AssetInfo& outInfo) const {
    return registry_->getAssetInfo(mediaId, outInfo);
}

uint32_t ProjectBrowserImpl::findAssetsByName(uint32_t nameStringId, MediaID* outResults, uint32_t maxResults) const {
    std::string searchName;
    if (!strings_->getString(nameStringId, searchName)) return 0;
    
    auto allIds = registry_->getAllMediaIDs();
    uint32_t count = 0;
    for (auto id : allIds) {
        if (count >= maxResults) break;
        AssetInfo info;
        if (registry_->getAssetInfo(id, info)) {
            std::string assetName;
            if (strings_->getString(info.nameId, assetName)) {
                if (assetName.find(searchName) != std::string::npos) {
                    outResults[count++] = id;
                }
            }
        }
    }
    return count;
}

bool ProjectBrowserImpl::removeAsset(MediaID mediaId) {
    return registry_->removeAsset(mediaId);
}

void ProjectBrowserImpl::update() {
    std::vector<CompletedJob> localCompleted;
    {
        std::unique_lock lock(completedMutex_);
        localCompleted = std::move(completedJobs_);
    }
    
    for (const auto& job : localCompleted) {
        if (job.callback) {
            job.callback(job.context, job.result, job.success, job.error.empty() ? nullptr : job.error.c_str());
        }
        
        std::unique_lock lock(mutex_);
        auto it = jobs_.find(job.jobId);
        if (it != jobs_.end()) {
            it->second->status = job.success ? ImportStatus::COMPLETE : ImportStatus::FAILED;
            it->second->resultId = job.result;
            if (!job.error.empty()) {
                std::strncpy(it->second->errorMessage, job.error.c_str(), sizeof(it->second->errorMessage) - 1);
            }
        }
    }
}

void ProjectBrowserImpl::workerLoop() {
    while (running_) {
        std::shared_ptr<InternalJob> job;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this]() { return !running_ || !pendingQueue_.empty(); });
            
            if (!running_) break;
            
            uint64_t jobId = pendingQueue_.front();
            pendingQueue_.pop();
            
            auto it = jobs_.find(jobId);
            if (it != jobs_.end()) {
                job = it->second;
            }
        }
        
        if (job) {
            processJob(*job);
        }
    }
}

void ProjectBrowserImpl::processJob(InternalJob& job) {
    if (job.cancelled) return;

    std::string filePath;
    if (!strings_->getString(job.filePathId, filePath)) {
        CompletedJob completed;
        completed.jobId = job.jobId;
        completed.success = false;
        completed.error = "Invalid file path ID";
        completed.callback = job.callback;
        completed.context = job.context;
        
        std::unique_lock lock(completedMutex_);
        completedJobs_.push_back(std::move(completed));
        return;
    }

    job.status = ImportStatus::DECODING;
    
    auto result = intakePipeline_->processAsset(filePath, job.options, [&](float progress) {
        job.progress = progress;
    });

    if (job.cancelled) return;

    CompletedJob completed;
    completed.jobId = job.jobId;
    completed.result = result.mediaId;
    completed.success = result.success;
    completed.error = result.errorMessage;
    completed.callback = job.callback;
    completed.context = job.context;
    
    if (result.success) {
        job.status = ImportStatus::COMPLETE;
        job.progress = 1.0f;
        job.resultId = result.mediaId;
    } else {
        job.status = ImportStatus::FAILED;
        std::strncpy(job.errorMessage, result.errorMessage.c_str(), sizeof(job.errorMessage) - 1);
    }

    std::unique_lock lock(completedMutex_);
    completedJobs_.push_back(std::move(completed));
}

std::unique_ptr<IProjectBrowser> IProjectBrowser::create(IMediaRegistry* registry, 
                                                       Layer2::IStringRegistry* strings,
                                                       ICodecFactory* codecs,
                                                       IAudioAnalysisEngine* analysis,
                                                       IWaveformRenderer* waveforms) {
    return std::make_unique<ProjectBrowserImpl>(registry, strings, codecs, analysis, waveforms);
}

} // namespace MediaManagement
