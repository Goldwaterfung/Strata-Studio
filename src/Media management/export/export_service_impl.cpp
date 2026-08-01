#include "iexport_service.h"
#include "Media management/registry/imedia_registry.h"
#include "Media management/codecs/sndfile_writer.h"
#include "Core audio engine/scheduler/idsp_kernel.h"
#include "Core infrastructure/memory/istring_registry.h"
#include <ebur128.h>
#include <mutex>
#include <thread>
#include <atomic>
#include <queue>
#include <unordered_map>
#include <condition_variable>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <random>

namespace MediaManagement {

class ExportServiceImpl : public IExportService {
public:
    explicit ExportServiceImpl(IMediaRegistry* registry, Layer2::IStringRegistry* strings, Layer3::IDSPKernel* kernel, const IMidiClipDataProvider* midiProvider)
        : registry_(registry), strings_(strings), kernel_(kernel), midiProvider_(midiProvider) {
        (void)registry_;
        workerThread_ = std::thread(&ExportServiceImpl::workerLoop, this);
    }

    ~ExportServiceImpl() override {
        running_ = false;
        cv_.notify_all();
        if (workerThread_.joinable()) {
            workerThread_.join();
        }
    }

    uint64_t exportRangeAsync(const ExportConfig& config, CompletionCallback callback, void* context) override {
        std::unique_lock lock(mutex_);
        uint64_t jobId = nextJobId_++;
        
        auto progress = std::make_shared<ExportProgress>();
        progress->jobId = jobId;
        progress->status = ExportStatus::PENDING;
        progress->progress = 0.0f;
        
        jobs_[jobId] = { config, callback, nullptr, context, progress, {} };
        jobQueue_.push(jobId);
        
        cv_.notify_one();
        return jobId;
    }

    uint64_t analyzeSessionLoudnessAsync(uint64_t startSample,
                                         uint64_t endSample,
                                         uint32_t sampleRate,
                                         uint16_t numChannels,
                                         AnalysisCallback callback,
                                         void* context) override {
        std::unique_lock lock(mutex_);
        uint64_t jobId = nextJobId_++;
        
        auto progress = std::make_shared<ExportProgress>();
        progress->jobId = jobId;
        progress->status = ExportStatus::PENDING;
        progress->progress = 0.0f;
        
        ExportConfig config{};
        config.startSample = startSample;
        config.endSample = endSample;
        config.sampleRate = sampleRate;
        config.numChannels = numChannels;
        
        jobs_[jobId] = { config, nullptr, callback, context, progress, {} };
        jobQueue_.push(jobId);
        
        cv_.notify_one();
        return jobId;
    }

    bool getProgress(uint64_t jobId, ExportProgress& outProgress) const override {
        std::unique_lock lock(mutex_);
        auto it = jobs_.find(jobId);
        if (it != jobs_.end()) {
            outProgress = *(it->second.progress);
            return true;
        }
        return false;
    }

    bool cancelExport(uint64_t jobId) override {
        std::unique_lock lock(mutex_);
        auto it = jobs_.find(jobId);
        if (it != jobs_.end()) {
            it->second.progress->status = ExportStatus::CANCELLED;
            return true;
        }
        return false;
    }

    void update() override {
        std::vector<JobCompletion> completions;
        {
            std::unique_lock lock(completedMutex_);
            completions = std::move(completedJobs_);
        }
        
        for (const auto& completion : completions) {
            std::unique_lock lock(mutex_);
            auto it = jobs_.find(completion.jobId);
            if (it != jobs_.end()) {
                if (it->second.analysisCallback) {
                    it->second.analysisCallback(completion.jobId,
                                                completion.progress->status == ExportStatus::COMPLETED,
                                                it->second.analysisResult,
                                                completion.progress->errorMessage,
                                                it->second.context);
                } else if (it->second.callback) {
                    it->second.callback(completion.jobId, 
                                      completion.progress->status == ExportStatus::COMPLETED, 
                                      completion.progress->errorMessage,
                                      it->second.context);
                }
                jobs_.erase(it);
            }
        }
    }

    //=== Capability Queries ===//

    bool isFormatSupported(ExportFormat format) const override {
        switch (format) {
            case ExportFormat::WAV:
            case ExportFormat::AIFF:
            case ExportFormat::FLAC:
            case ExportFormat::OGG:
                return true;
            case ExportFormat::MP3:
                return false; // MP3 support requires additional codec integration
            default:
                return false;
        }
    }

    void getSupportedSampleRates(uint32_t* rates, uint32_t* count) const override {
        static const uint32_t standardRates[] = { 44100, 48000, 88200, 96000, 176400, 192000 };
        uint32_t capacity = *count;
        *count = std::min(capacity, static_cast<uint32_t>(sizeof(standardRates) / sizeof(uint32_t)));
        for (uint32_t i = 0; i < *count; ++i) {
            rates[i] = standardRates[i];
        }
    }

    void getSupportedBitDepths(ExportFormat format, ExportBitDepth* depths, uint32_t* count) const override {
        (void)format; // For now, all supported formats support all bit depths in this impl
        static const ExportBitDepth standardDepths[] = { 
            ExportBitDepth::BIT_16, 
            ExportBitDepth::BIT_24, 
            ExportBitDepth::BIT_32_FLOAT 
        };
        uint32_t capacity = *count;
        *count = std::min(capacity, static_cast<uint32_t>(sizeof(standardDepths) / sizeof(ExportBitDepth)));
        for (uint32_t i = 0; i < *count; ++i) {
            depths[i] = standardDepths[i];
        }
    }

private:
    struct JobInternal {
        ExportConfig config;
        CompletionCallback callback;
        AnalysisCallback analysisCallback;
        void* context;
        std::shared_ptr<ExportProgress> progress;
        AnalysisResult analysisResult;
    };

    struct JobCompletion {
        uint64_t jobId;
        std::shared_ptr<ExportProgress> progress;
    };

    void workerLoop() {
        while (running_) {
            uint64_t jobId = 0;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this]() { return !running_ || !jobQueue_.empty(); });
                if (!running_) break;
                jobId = jobQueue_.front();
                jobQueue_.pop();
            }
            
            auto it = jobs_.find(jobId);
            if (it != jobs_.end()) {
                processExport(it->second);
                
                std::unique_lock lock(completedMutex_);
                completedJobs_.push_back({jobId, it->second.progress});
            }
        }
    }

    void processExport(JobInternal& job) {
        if (job.analysisCallback) {
            processSilentAnalysis(job);
        } else if (job.config.stemExport) {
            processStemExport(job);
        } else {
            processSingleExport(job, job.config.outputPathId, NodeID::invalid());
        }
    }

    void processSilentAnalysis(JobInternal& job) {
        job.progress->status = ExportStatus::PREPARING;
        
        uint64_t totalFrames = job.config.endSample - job.config.startSample;
        uint64_t processedFrames = 0;
        
        // Initialize ebur128 state
        ebur128_state* ebustate = ebur128_init(job.config.numChannels, job.config.sampleRate, EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK);
        if (!ebustate) {
            job.progress->status = ExportStatus::FAILED;
            std::strncpy(job.progress->errorMessage, "Failed to initialize ebur128 state", 127);
            return;
        }

        job.progress->status = ExportStatus::PROCESSING;

        std::vector<float> outputData(1024 * job.config.numChannels);
        std::vector<float*> outputPlanes(job.config.numChannels);
        for (uint32_t c = 0; c < job.config.numChannels; ++c) {
            outputPlanes[c] = &outputData[c * 1024];
        }
        
        // Interleaved buffer for ebur128
        std::vector<float> interleaved(1024 * job.config.numChannels);

        ProcessContext context{};
        context.sampleRate = static_cast<float>(job.config.sampleRate);
        context.isOffline = true;
        context.maxBlockSize = 1024;
        context.isolateNodeId = NodeID::invalid();
        context.transportState = TransportState::PLAYING;
        context.timelineSnapshot = kernel_->getActiveTimelineSnapshot();
        context.midiClipDataProvider = midiProvider_;

        // Pre-roll / Flush Latency
        uint32_t totalLatency = kernel_->getTotalLatency();
        if (totalLatency > 0) {
            uint32_t remainingPreRoll = totalLatency;
            while (remainingPreRoll > 0) {
                uint32_t framesToProcess = std::min(remainingPreRoll, 1024u);
                int64_t preRollPos = static_cast<int64_t>(job.config.startSample) - static_cast<int64_t>(remainingPreRoll);
                context.transport.positionSample = static_cast<uint64_t>(preRollPos);
                context.currentBlockSize = framesToProcess;

                kernel_->process(nullptr, outputPlanes.data(), job.config.numChannels, framesToProcess, &context);
                remainingPreRoll -= framesToProcess;
            }
        }

        bool clippingDetected = false;

        while (processedFrames < totalFrames && job.progress->status == ExportStatus::PROCESSING) {
            std::atomic_thread_fence(std::memory_order_acquire);
            uint32_t toProcess = static_cast<uint32_t>(std::min(static_cast<uint64_t>(1024), totalFrames - processedFrames));
            context.currentBlockSize = toProcess;
            context.transport.positionSample = job.config.startSample + processedFrames;

            kernel_->process(nullptr, outputPlanes.data(), job.config.numChannels, toProcess, &context);

            // Interleave and check clipping/peak levels
            for (uint32_t f = 0; f < toProcess; ++f) {
                for (uint32_t c = 0; c < job.config.numChannels; ++c) {
                    float val = outputPlanes[c][f];
                    float absVal = std::abs(val);
                    if (absVal > 1.0f) {
                        clippingDetected = true;
                    }
                    interleaved[f * job.config.numChannels + c] = val;
                }
            }

            ebur128_add_frames_float(ebustate, interleaved.data(), toProcess);
            processedFrames += toProcess;
            job.progress->progress = static_cast<float>(processedFrames) / (totalFrames > 0 ? totalFrames : 1);
        }

        if (job.progress->status == ExportStatus::PROCESSING) {
            // Retrieve integrated loudness
            double integrated = -70.0;
            ebur128_loudness_global(ebustate, &integrated);

            // Retrieve true peak
            double truePeak = -100.0;
            for (uint32_t c = 0; c < job.config.numChannels; ++c) {
                double chPeak = 0.0;
                ebur128_true_peak(ebustate, c, &chPeak);
                double chPeakdB = 20.0 * std::log10(std::max(1e-10, chPeak));
                truePeak = std::max(truePeak, chPeakdB);
            }

            job.analysisResult.integratedLoudnessLUFS = static_cast<float>(integrated);
            job.analysisResult.truePeakDBTP = static_cast<float>(truePeak);
            job.analysisResult.clippingDetected = clippingDetected;

            job.progress->status = ExportStatus::COMPLETED;
            job.progress->progress = 1.0f;
        }

        ebur128_destroy(&ebustate);
    }

    void processSingleExport(JobInternal& job, uint32_t outputPathId, NodeID isolateNode) {
        job.progress->status = ExportStatus::PREPARING;
        
        std::string outputPath;
        if (!strings_->getString(outputPathId, outputPath)) {
            job.progress->status = ExportStatus::FAILED;
            std::strncpy(job.progress->errorMessage, "Invalid output path ID", 127);
            return;
        }

        int sfFormat = formatToSndFile(job.config.format, job.config.bitDepth);
        (void)job.config.endSample; // Suppress unused if needed, or use it
        (void)job.config.startSample;
        
        float peak = 0.0f;
        float gainMultiplier = 1.0f;

        // Pass 1: Normalization (if enabled)
        if (job.config.normalize) {
            peak = performNormalizationPass(job, isolateNode);
            if (peak > 0.000001f) {
                float targetPeak = std::pow(10.0f, job.config.normalizationdB / 20.0f);
                gainMultiplier = targetPeak / peak;
            }
        }

        // Pass 2: Final Render
        SndFileWriter writer(outputPath, 
                            static_cast<int>(job.config.sampleRate), 
                            static_cast<int>(job.config.numChannels), 
                            sfFormat);
        
        if (!writer.isValid()) {
            job.progress->status = ExportStatus::FAILED;
            std::strncpy(job.progress->errorMessage, "Failed to create output file", 127);
            return;
        }

        applyMetadata(job, writer);

        job.progress->status = ExportStatus::PROCESSING;
        renderToWriter(job, writer, gainMultiplier, isolateNode);

        if (job.progress->status == ExportStatus::PROCESSING) {
            job.progress->status = ExportStatus::COMPLETED;
            job.progress->progress = 1.0f;
        }
        
        writer.close();
    }

    void processStemExport(JobInternal& job) {
        // For stems, we append the node ID or name to the path
        std::string basePath;
        strings_->getString(job.config.outputPathId, basePath);
        
        size_t dotPos = basePath.find_last_of('.');
        std::string pathNoExt = (dotPos == std::string::npos) ? basePath : basePath.substr(0, dotPos);
        std::string ext = (dotPos == std::string::npos) ? "" : basePath.substr(dotPos);

        for (uint32_t i = 0; i < job.config.numStemNodes; ++i) {
            NodeID node = job.config.stemNodes[i];
            std::string stemPath = pathNoExt + "_stem_" + std::to_string(node.id) + ext;
            uint32_t stemPathId = strings_->registerString(stemPath);
            
            processSingleExport(job, stemPathId, node);
            
            if (job.progress->status == ExportStatus::CANCELLED || job.progress->status == ExportStatus::FAILED) {
                break;
            }
        }
    }

    float performNormalizationPass(JobInternal& job, NodeID isolateNode) {
        (void)isolateNode;
        uint64_t totalFrames = job.config.endSample - job.config.startSample;
        uint64_t processedFrames = 0;
        float maxPeak = 0.0f;

        // Reset transport/kernel state
        // TODO: kernel_->reset() or similar if needed
        
        std::vector<float> outputData(1024 * job.config.numChannels);
        std::vector<float*> outputPlanes(job.config.numChannels);
        for(uint32_t c=0; c < job.config.numChannels; ++c) outputPlanes[c] = &outputData[c * 1024];

        ProcessContext context{};
        context.sampleRate = static_cast<float>(job.config.sampleRate);
        context.isOffline = true;
        context.maxBlockSize = 1024;
        context.isolateNodeId = isolateNode;
        context.transportState = TransportState::PLAYING;

        while (processedFrames < totalFrames && job.progress->status != ExportStatus::CANCELLED) {
            std::atomic_thread_fence(std::memory_order_acquire);
            uint32_t toProcess = static_cast<uint32_t>(std::min(static_cast<uint64_t>(1024), totalFrames - processedFrames));
            context.currentBlockSize = toProcess;
            context.transport.positionSample = job.config.startSample + processedFrames + kernel_->getTotalLatency();

            kernel_->process(nullptr, outputPlanes.data(), job.config.numChannels, toProcess, &context);

            for (uint32_t i = 0; i < toProcess * job.config.numChannels; ++i) {
                maxPeak = std::max(maxPeak, std::abs(outputData[i]));
            }

            processedFrames += toProcess;
        }

        return maxPeak;
    }

    void renderToWriter(JobInternal& job, SndFileWriter& writer, float gain, NodeID isolateNode) {
        (void)isolateNode;
        uint64_t totalFrames = job.config.endSample - job.config.startSample;
        uint64_t processedFrames = 0;
        
        std::vector<float> outputData(1024 * job.config.numChannels);
        std::vector<float*> outputPlanes(job.config.numChannels);
        for(uint32_t c=0; c < job.config.numChannels; ++c) outputPlanes[c] = &outputData[c * 1024];
        
        std::vector<float> interleaved(1024 * job.config.numChannels);

        ProcessContext context{};
        context.sampleRate = static_cast<float>(job.config.sampleRate);
        context.isOffline = true;
        context.maxBlockSize = 1024;
        context.isolateNodeId = isolateNode;
        context.transportState = TransportState::PLAYING;

        std::mt19937 gen(42); // Deterministic seed for dither
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

        // Calculate dither scale based on bit depth
        float ditherScale = 0.0f;
        if (job.config.dither != DitherType::NONE) {
            if (job.config.bitDepth == ExportBitDepth::BIT_16) ditherScale = 1.0f / 32768.0f;
            else if (job.config.bitDepth == ExportBitDepth::BIT_24) ditherScale = 1.0f / 8388608.0f;
        }

        // Pre-roll / Flush Latency
        uint32_t totalLatency = kernel_->getTotalLatency();
        if (totalLatency > 0) {
            uint32_t remainingPreRoll = totalLatency;
            while (remainingPreRoll > 0) {
                uint32_t framesToProcess = std::min(remainingPreRoll, 1024u);
                int64_t preRollPos = static_cast<int64_t>(job.config.startSample) - static_cast<int64_t>(remainingPreRoll);
                context.transport.positionSample = static_cast<uint64_t>(preRollPos);
                context.currentBlockSize = framesToProcess;

                kernel_->process(nullptr, outputPlanes.data(), job.config.numChannels, framesToProcess, &context);
                remainingPreRoll -= framesToProcess;
            }
        }

        while (processedFrames < totalFrames && job.progress->status == ExportStatus::PROCESSING) {
            std::atomic_thread_fence(std::memory_order_acquire);
            uint32_t toProcess = static_cast<uint32_t>(std::min(static_cast<uint64_t>(1024), totalFrames - processedFrames));
            context.currentBlockSize = toProcess;
            context.transport.positionSample = job.config.startSample + processedFrames;

            kernel_->process(nullptr, outputPlanes.data(), job.config.numChannels, toProcess, &context);

            // Interleave, Apply Gain, and Dither
            for (uint32_t f = 0; f < toProcess; ++f) {
                for (uint32_t c = 0; c < job.config.numChannels; ++c) {
                    float val = outputPlanes[c][f] * gain;
                    
                    if (ditherScale > 0.0f) {
                        float d = (dist(gen) + dist(gen)) * ditherScale; // TPDF
                        val += d;
                    }
                    
                    interleaved[f * job.config.numChannels + c] = val;
                }
            }

            writer.writeFrames(interleaved.data(), toProcess);
            processedFrames += toProcess;
            job.progress->progress = static_cast<float>(processedFrames) / totalFrames;
        }
    }

    void applyMetadata(JobInternal& job, SndFileWriter& writer) {
        auto setMeta = [&](uint32_t id, int sfKey) {
            std::string val;
            if (id != 0 && strings_->getString(id, val)) {
                writer.setStringMetadata(sfKey, val.c_str());
            }
        };

        setMeta(job.config.titleId, SF_STR_TITLE);
        setMeta(job.config.artistId, SF_STR_ARTIST);
        setMeta(job.config.albumId, SF_STR_ALBUM);
        setMeta(job.config.genreId, SF_STR_GENRE);
        setMeta(job.config.commentId, SF_STR_COMMENT);
    }

    static int formatToSndFile(ExportFormat format, ExportBitDepth bitDepth) {
        int sfFormat = 0;
        switch (format) {
            case ExportFormat::WAV:  sfFormat = SF_FORMAT_WAV; break;
            case ExportFormat::AIFF: sfFormat = SF_FORMAT_AIFF; break;
            case ExportFormat::FLAC: sfFormat = SF_FORMAT_FLAC; break;
            case ExportFormat::OGG:  sfFormat = SF_FORMAT_OGG; break;
            case ExportFormat::MP3:  
#ifdef SF_FORMAT_MPEG
                sfFormat = SF_FORMAT_MPEG;
#else
                sfFormat = SF_FORMAT_WAV;
#endif
                break;
        }
        
        switch (bitDepth) {
            case ExportBitDepth::BIT_16:       sfFormat |= SF_FORMAT_PCM_16; break;
            case ExportBitDepth::BIT_24:       sfFormat |= SF_FORMAT_PCM_24; break;
            case ExportBitDepth::BIT_32_FLOAT: sfFormat |= SF_FORMAT_FLOAT; break;
        }
        return sfFormat;
    }

    IMediaRegistry* registry_;
    Layer2::IStringRegistry* strings_;
    Layer3::IDSPKernel* kernel_;
    const IMidiClipDataProvider* midiProvider_;
    mutable std::mutex mutex_;
    std::atomic<bool> running_{true};
    std::thread workerThread_;
    std::condition_variable cv_;
    std::atomic<uint64_t> nextJobId_{1};
    std::unordered_map<uint64_t, JobInternal> jobs_;
    std::queue<uint64_t> jobQueue_;

    std::mutex completedMutex_;
    std::vector<JobCompletion> completedJobs_;
};

std::unique_ptr<IExportService> IExportService::create(IMediaRegistry* registry, Layer2::IStringRegistry* strings, Layer3::IDSPKernel* kernel, const IMidiClipDataProvider* midiProvider) {
    return std::make_unique<ExportServiceImpl>(registry, strings, kernel, midiProvider);
}

} // namespace MediaManagement
