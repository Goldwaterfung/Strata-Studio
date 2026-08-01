#include "audio_analysis_engine_impl.h"
#include "Media management/registry/imedia_registry.h"
#include "Media management/codecs/icodec_factory.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "bpm_analyzer.h"
#include "common/math/analysis.h"
#include <ebur128.h>
#include <unsupported/Eigen/FFT>
#include <vector>
#include <algorithm>
#include <cmath>
#include "Media management/intake/imedia_intake_pipeline.h"

namespace MediaManagement {

AudioAnalysisEngineImpl::AudioAnalysisEngineImpl(IMediaRegistry* registry, 
                                               Layer2::IStringRegistry* strings,
                                               ICodecFactory* codecFactory,
                                               uint32_t fftSize)
    : registry_(registry), strings_(strings), codecFactory_(codecFactory), fftSize_(fftSize)
{
    workerThread_ = std::thread(&AudioAnalysisEngineImpl::workerLoop, this);
    
    // Pre-allocate update swap buffer to reduce allocations on main thread
    updateSwapBuffer_.reserve(16);
}

AudioAnalysisEngineImpl::~AudioAnalysisEngineImpl() {
    running_ = false;
    cv_.notify_all();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

// --- IAudioAnalysisEngine Interface ---

void AudioAnalysisEngineImpl::analyzeRealtime(const struct AudioBuffer* input,
                                             uint32_t numSamples,
                                             AnalysisResult& outResult) {
    (void)input;
    (void)numSamples;
    (void)outResult;
    // Tier 1: Real-time analysis (RT-Safe).
    // TODO: Implement incremental analysis for live input.
}

bool AudioAnalysisEngineImpl::analyze(MediaID mediaId, AnalysisResult& result) {
    return performAnalysis(mediaId, result, nullptr);
}

bool AudioAnalysisEngineImpl::analyzeAsync(MediaID mediaId, 
                                         CompletionCallback callback,
                                         void* context) {
    std::unique_lock lock(mutex_);
    jobQueue_.push({ mediaId, callback, context });
    cv_.notify_one();
    return true;
}

void AudioAnalysisEngineImpl::getSpectralFluxData(MediaID mediaId, float* buffer, uint32_t bufferSize) {
    AssetAnalysisBlobs blobs;
    if (registry_->getAnalysisBlobs(mediaId, blobs) && buffer) {
        const auto& flux = blobs.spectralFlux;
        size_t toCopy = std::min(static_cast<size_t>(bufferSize), flux.size());
        std::copy(flux.begin(), flux.begin() + static_cast<ptrdiff_t>(toCopy), buffer);
    }
}

void AudioAnalysisEngineImpl::getTransientData(MediaID mediaId, uint64_t* positions, float* amplitudes, uint32_t bufferSize) {
    AssetAnalysisBlobs blobs;
    if (registry_->getAnalysisBlobs(mediaId, blobs)) {
        const auto& pos = blobs.transientPositions;
        const auto& amp = blobs.transientAmplitudes;
        size_t toCopy = std::min({static_cast<size_t>(bufferSize), pos.size(), amp.size()});
        if (positions) std::copy(pos.begin(), pos.begin() + static_cast<ptrdiff_t>(toCopy), positions);
        if (amplitudes) std::copy(amp.begin(), amp.begin() + static_cast<ptrdiff_t>(toCopy), amplitudes);
    }
}

void AudioAnalysisEngineImpl::getPitchData(MediaID mediaId, float* buffer, uint32_t bufferSize) {
    AssetAnalysisBlobs blobs;
    if (registry_->getAnalysisBlobs(mediaId, blobs) && buffer) {
        const auto& pitch = blobs.pitchData;
        size_t toCopy = std::min(static_cast<size_t>(bufferSize), pitch.size());
        std::copy(pitch.begin(), pitch.begin() + static_cast<ptrdiff_t>(toCopy), buffer);
    }
}

void AudioAnalysisEngineImpl::calculateLoudness(MediaID mediaId, float* integrated, float* range, float* truePeak) {
    AnalysisResult res{};
    if (performAnalysis(mediaId, res)) {
        if (integrated) *integrated = res.integratedLUFS;
        if (range) *range = res.loudnessRange;
        if (truePeak) *truePeak = res.truePeakdB;
    }
}

void AudioAnalysisEngineImpl::detectTempo(MediaID mediaId, float* tempo, float* confidence) {
    AnalysisResult res{};
    if (performAnalysis(mediaId, res)) {
        if (tempo) *tempo = res.tempo;
        if (confidence) *confidence = res.tempoConfidence;
    }
}

void AudioAnalysisEngineImpl::detectKey(MediaID mediaId, uint8_t* root, bool* isMinor, float* confidence) {
    AnalysisResult res{};
    if (performAnalysis(mediaId, res)) {
        if (root) *root = res.keyRoot;
        if (isMinor) *isMinor = res.isMinor;
        if (confidence) *confidence = res.keyConfidence;
    }
}

void AudioAnalysisEngineImpl::update() {
    // Swap buffer to minimize lock contention and avoid allocations
    updateSwapBuffer_.clear();
    {
        std::unique_lock lock(resultsMutex_);
        if (completedResults_.empty()) return;
        std::swap(updateSwapBuffer_, completedResults_);
    }
    
    for (const auto& completed : updateSwapBuffer_) {
        if (completed.callback) {
            completed.callback(completed.context, completed.result);
        }
    }
}

// --- Internal ---

void AudioAnalysisEngineImpl::workerLoop() {
    while (running_) {
        AnalysisJob job;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this]() { return !running_ || !jobQueue_.empty(); });
            
            if (!running_) break;
            
            job = std::move(jobQueue_.front());
            jobQueue_.pop();
        }
        
        processJob(job);
    }
}

void AudioAnalysisEngineImpl::processJob(const AnalysisJob& job) {
    CompletedResult completed;
    completed.callback = job.callback;
    completed.context = job.context;
    
    AssetAnalysisBlobs blobs;
    if (performAnalysis(job.mediaId, completed.result, &blobs)) {
        // Cache large data in Registry
        registry_->setAnalysisBlobs(job.mediaId, blobs);
    }

    {
        std::unique_lock lock(resultsMutex_);
        completedResults_.push_back(std::move(completed));
    }
}

bool AudioAnalysisEngineImpl::performAnalysis(MediaID mediaId, AnalysisResult& result, AssetAnalysisBlobs* largeData) {
    AssetInfo info{};
    if (!registry_->getAssetInfo(mediaId, info)) return false;

    std::string filePath;
    if (!strings_->getString(info.pathId, filePath)) return false;

    auto reader = codecFactory_->createReader(filePath);
    if (!reader || !reader->isValid()) return false;

    uint32_t channels = reader->getNumChannels();
    uint32_t sampleRate = reader->getSampleRate();
    
    // 1. Loudness Analysis (EBU R128)
    ebur128_state* ebustate = ebur128_init(channels, sampleRate, EBUR128_MODE_I | EBUR128_MODE_LRA | EBUR128_MODE_TRUE_PEAK | EBUR128_MODE_M | EBUR128_MODE_S);
    
    // 2. BPM Analysis
    auto bpmResult = BPMAnalyzer::analyze(mediaId, reader.get(), nullptr);
    result.tempo = bpmResult.bpm;
    result.tempoConfidence = bpmResult.confidence;
    result.tempoPositionSample = 0; // Global for the whole file

    // Reset reader for second pass
    reader->seek(0);
    
    std::vector<float> buffer(4096 * channels);
    uint32_t framesRead = 0;
    float globalPeak = 0.0f;
    double sumSquared = 0.0;
    uint64_t totalFrames = 0;

    while ((framesRead = reader->readFrames(buffer.data(), 4096)) > 0) {
        ebur128_add_frames_float(ebustate, buffer.data(), framesRead);
        for (size_t i = 0; i < static_cast<size_t>(framesRead) * channels; ++i) {
            float val = std::abs(buffer[i]);
            if (val > globalPeak) globalPeak = val;
            sumSquared += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
        }
        totalFrames += framesRead;
    }

    // Loudness results
    double integrated = 0, range = 0, truePeak = -100.0;
    ebur128_loudness_global(ebustate, &integrated);
    ebur128_loudness_range(ebustate, &range);
    
    for (uint32_t c = 0; c < channels; ++c) {
        double chPeak = 0;
        ebur128_true_peak(ebustate, c, &chPeak);
        double chPeakdB = 20.0 * std::log10(std::max(1e-10, chPeak));
        truePeak = std::max(truePeak, chPeakdB);
    }

    result.integratedLUFS = static_cast<float>(integrated);
    result.loudnessRange = static_cast<float>(range);
    result.truePeakdB = static_cast<float>(truePeak);
    
    double mLUFS = 0, sLUFS = 0;
    ebur128_loudness_momentary(ebustate, &mLUFS);
    ebur128_loudness_shortterm(ebustate, &sLUFS);
    result.momentaryLUFS = static_cast<float>(mLUFS);
    result.shortTermLUFS = static_cast<float>(sLUFS);

    ebur128_destroy(&ebustate);

    // Peak and RMS
    result.peakDecibels = 20.0f * std::log10(std::max(1e-10f, globalPeak));
    result.rmsDecibels = static_cast<float>(10.0 * std::log10(std::max(1e-10, sumSquared / (static_cast<double>(totalFrames) * static_cast<double>(channels)))));
    result.dynamicRange = result.peakDecibels - result.rmsDecibels;

    // Spectral and Transient Analysis
    Eigen::FFT<float> fft;
    uint32_t hopSize = fftSize_ / 2;
    std::vector<float> window(fftSize_);
    const float PI_F = 3.14159265358979323846f;
    for (uint32_t i = 0; i < fftSize_; ++i) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * PI_F * static_cast<float>(i) / static_cast<float>(fftSize_ - 1)));
    }

    std::vector<float> prevMag(fftSize_ / 2 + 1, 0.0f);
    std::vector<float> currentMag(fftSize_ / 2 + 1, 0.0f);
    std::vector<std::complex<float>> spec(fftSize_);
    std::vector<float> timeBuffer(fftSize_, 0.0f);
    
    // Reset reader for spectral pass
    reader->seek(0);
    
    float avgFlux = 0.0f;
    uint32_t fluxCount = 0;

    // Buffers for transient detection
    float fluxThreshold = 0.0f;
    std::vector<float> fluxHistory;
    
    while (reader->readFrames(buffer.data(), hopSize) == hopSize) {
        // Shift time buffer and read new frames
        std::move(timeBuffer.begin() + hopSize, timeBuffer.end(), timeBuffer.begin());
        // Mix to mono for analysis
        for (uint32_t i = 0; i < hopSize; ++i) {
            float mono = 0;
            for (uint32_t c = 0; c < channels; ++c) mono += buffer[i * channels + c];
            timeBuffer[fftSize_ - hopSize + i] = mono / channels;
        }

        // Apply window
        std::vector<float> windowed(fftSize_);
        for (uint32_t i = 0; i < fftSize_; ++i) windowed[i] = timeBuffer[i] * window[i];

        // FFT
        fft.fwd(spec, windowed);

        // Magnitude Spectrum and Spectral Centroid
        float centroidNumerator = 0;
        float centroidDenominator = 0;
        float flux = 0;

        for (uint32_t i = 0; i <= fftSize_ / 2; ++i) {
            currentMag[i] = std::abs(spec[i]);
            float freq = static_cast<float>(i) * sampleRate / fftSize_;
            centroidNumerator += freq * currentMag[i];
            centroidDenominator += currentMag[i];

            // Spectral Flux (Half-wave rectified)
            float diff = currentMag[i] - prevMag[i];
            if (diff > 0) flux += diff;
        }

        if (centroidDenominator > 1e-6f) {
            result.spectralCentroid = (result.spectralCentroid * fluxCount + (centroidNumerator / centroidDenominator)) / (fluxCount + 1);
        }

        if (largeData) {
            largeData->spectralFlux.push_back(flux);
        }
        
        fluxHistory.push_back(flux);
        avgFlux += flux;
        fluxCount++;
        prevMag = currentMag;
    }

    result.spectralFluxCount = fluxCount;
    avgFlux /= std::max(1u, fluxCount);

    // Simple Transient Detection using Spectral Flux peaks
    if (fluxCount > 0) {
        fluxThreshold = avgFlux * 2.5f; // Heuristic multiplier
        for (uint32_t i = 1; i < fluxCount - 1; ++i) {
            if (fluxHistory[i] > fluxThreshold && fluxHistory[i] > fluxHistory[i-1] && fluxHistory[i] > fluxHistory[i+1]) {
                result.transientCount++;
                if (largeData) {
                    largeData->transientPositions.push_back(static_cast<uint64_t>(i) * hopSize);
                    largeData->transientAmplitudes.push_back(fluxHistory[i]);
                }
            }
        }
    }

    // Key Detection using Math::Analysis::detectKeyFromChromagram
    std::array<float, 12> chroma = {0.0f};
    if (fluxCount > 0) {
        const uint32_t numBins = fftSize_ / 2 + 1;
        for (uint32_t bin = 1; bin < numBins; ++bin) {
            float freq = static_cast<float>(bin) * sampleRate / static_cast<float>(fftSize_);
            if (freq >= 27.5f && freq <= 4186.0f) { // A0 to C8
                int pitchClass = static_cast<int>(std::round(12.0f * std::log2(freq / 440.0f) + 69.0f)) % 12;
                if (pitchClass < 0) pitchClass += 12;
                chroma[static_cast<size_t>(pitchClass)] += currentMag[bin];
            }
        }
    }

    const auto keyEst = Math::Analysis::detectKeyFromChromagram(chroma);
    result.keyRoot = keyEst.root;
    result.isMinor = keyEst.isMinor;
    result.keyConfidence = keyEst.confidence;
    result.keyPositionSample = 0;

    // Update Registry
    if (registry_->getAssetInfo(mediaId, info)) {
        info.analysis.tempo = result.tempo;
        info.analysis.tempoConfidence = result.tempoConfidence;
        info.analysis.analyzed = true;
        registry_->updateAssetInfo(mediaId, info);
    }

    return true;
}

/**
 * @brief Concrete implementation of IAnalysisSink.
 */
class AnalysisSinkImpl : public IAnalysisSink {
public:
    AnalysisSinkImpl([[maybe_unused]] uint32_t sampleRate, uint16_t numChannels)
        : numChannels_(numChannels) {
        ebustate_ = ebur128_init(numChannels, sampleRate, EBUR128_MODE_I | EBUR128_MODE_LRA | EBUR128_MODE_TRUE_PEAK | EBUR128_MODE_M | EBUR128_MODE_S);
        
        uint32_t fftSize = 2048; // Hardcoded for now
        window_.resize(fftSize);
        for (uint32_t i = 0; i < fftSize; ++i) {
            window_[i] = 0.5f * (1.0f - std::cos(2.0f * MathConstants::PI * static_cast<float>(i) / static_cast<float>(fftSize - 1)));
        }
        timeBuffer_.assign(fftSize, 0.0f);
        prevMag_.assign(fftSize / 2 + 1, 0.0f);
    }

    ~AnalysisSinkImpl() override {
        if (ebustate_) ebur128_destroy(&ebustate_);
    }

    void processFrames(const float* buffer, uint32_t numFrames, uint16_t numChannels) override {
        // 1. Loudness
        ebur128_add_frames_float(ebustate_, buffer, numFrames);

        // 2. Peak & RMS accumulation
        for (size_t i = 0; i < static_cast<size_t>(numFrames) * numChannels; ++i) {
            float val = std::abs(buffer[i]);
            if (val > globalPeak_) globalPeak_ = val;
            sumSquared_ += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
        }
        totalFrames_ += numFrames;

        // 3. Spectral Analysis (Simplified for this pass)
        // ... (Would need to handle hop size and overlap)
    }

    void finalize() override {
        // Finalize loudness
        double integrated = 0, range = 0, truePeak = -100.0;
        ebur128_loudness_global(ebustate_, &integrated);
        ebur128_loudness_range(ebustate_, &range);
        for (uint32_t c = 0; c < numChannels_; ++c) {
            double chPeak = 0;
            ebur128_true_peak(ebustate_, c, &chPeak);
            double chPeakdB = 20.0 * std::log10(std::max(1e-10, chPeak));
            truePeak = std::max(truePeak, chPeakdB);
        }

        result_.integratedLUFS = static_cast<float>(integrated);
        result_.loudnessRange = static_cast<float>(range);
        result_.truePeakdB = static_cast<float>(truePeak);
        
        double mLUFS = 0, sLUFS = 0;
        ebur128_loudness_momentary(ebustate_, &mLUFS);
        ebur128_loudness_shortterm(ebustate_, &sLUFS);
        result_.momentaryLUFS = static_cast<float>(mLUFS);
        result_.shortTermLUFS = static_cast<float>(sLUFS);

        result_.peakDecibels = 20.0f * std::log10(std::max(1e-10f, globalPeak_));
        result_.rmsDecibels = static_cast<float>(10.0 * std::log10(std::max(1e-10, sumSquared_ / (static_cast<double>(totalFrames_) * static_cast<double>(numChannels_)))));
        result_.dynamicRange = result_.peakDecibels - result_.rmsDecibels;
    }

    AnalysisResult getResult() const override {
        return result_;
    }

    // LargeAnalysisData could be retrieved here too if we extend the interface
    // but for now we focus on the POD.

private:
    uint16_t numChannels_;
    ebur128_state* ebustate_;
    float globalPeak_ = 0.0f;
    double sumSquared_ = 0.0;
    uint64_t totalFrames_ = 0;
    AnalysisResult result_{};

    // Spectral state
    std::vector<float> window_;
    std::vector<float> timeBuffer_;
    std::vector<float> prevMag_;
};

std::unique_ptr<IAnalysisSink> AudioAnalysisEngineImpl::createSink(uint32_t sampleRate, uint16_t numChannels) {
    return std::make_unique<AnalysisSinkImpl>(sampleRate, numChannels);
}

std::unique_ptr<IAudioAnalysisEngine> IAudioAnalysisEngine::create(IMediaRegistry* registry, 
                                                                  Layer2::IStringRegistry* strings,
                                                                  ICodecFactory* codecFactory,
                                                                  uint32_t fftSize) {
    return std::make_unique<AudioAnalysisEngineImpl>(registry, strings, codecFactory, fftSize);
}

} // namespace MediaManagement
