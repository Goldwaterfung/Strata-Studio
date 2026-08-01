#include "imedia_intake_pipeline.h"
#include "Media management/registry/imedia_registry.h"
#include "Media management/registry/media_primitives.h"
#include "Media management/codecs/icodec_factory.h"
#include "Media management/codecs/icodec_reader.h"
#include "Media management/analysis/iaudio_analysis_engine.h"
#include "Media management/waveforms/iwaveform_renderer.h"
#include "Core infrastructure/memory/istring_registry.h"
#include <filesystem>
#include <chrono>

namespace MediaManagement {

class MediaIntakePipelineImpl : public IMediaIntakePipeline {
public:
    MediaIntakePipelineImpl(IMediaRegistry* registry,
                            Layer2::IStringRegistry* strings,
                            ICodecFactory* codecs,
                            IAudioAnalysisEngine* analysis,
                            IWaveformRenderer* waveforms)
        : registry_(registry), strings_(strings), codecs_(codecs), 
          analysis_(analysis), waveforms_(waveforms) {}

    IntakeResult processAsset(const std::string& filePath,
                             const ImportOptions& options,
                             std::function<void(float)> progressCallback) override {
        
        // 1. Open Reader
        auto reader = codecs_->createReader(filePath);
        if (!reader || !reader->isValid()) {
            return { false, MediaID::invalid(), "Failed to open audio file" };
        }

        uint32_t sampleRate = reader->getSampleRate();
        uint16_t numChannels = reader->getNumChannels();
        uint64_t totalFrames = reader->getTotalFrames();

        // 2. Prepare Sinks
        std::vector<std::unique_ptr<IAudioStreamSink>> sinks;
        IAnalysisSink* analysisSink = nullptr;
        IWaveformSink* waveformSink = nullptr;

        if (options.analyzeAudio || options.detectTempo || options.detectKey) {
            auto aSink = analysis_->createSink(sampleRate, numChannels);
            analysisSink = aSink.get();
            sinks.push_back(std::move(aSink));
        }

        if (options.generateWaveform) {
            auto wSink = waveforms_->createSink(sampleRate, numChannels);
            waveformSink = wSink.get();
            sinks.push_back(std::move(wSink));
        }

        // 3. Streaming Loop
        std::vector<float> buffer(4096 * numChannels);
        uint64_t framesProcessed = 0;
        uint32_t framesRead = 0;

        while ((framesRead = reader->readFrames(buffer.data(), 4096)) > 0) {
            for (auto& sink : sinks) {
                sink->processFrames(buffer.data(), framesRead, numChannels);
            }
            framesProcessed += framesRead;
            
            if (progressCallback) {
                progressCallback(static_cast<float>(framesProcessed) / totalFrames * 0.9f);
            }
        }

        // 4. Finalize Sinks
        for (auto& sink : sinks) {
            sink->finalize();
        }

        // 5. Aggregate Results & Atomic Commit
        AssetInfo info{};
        info.sampleRate = sampleRate;
        info.numChannels = numChannels;
        info.durationSamples = totalFrames;
        info.bitDepth = reader->getBitDepth();
        info.sizeBytes = std::filesystem::file_size(filePath);
        
        std::string filename = std::filesystem::path(filePath).filename().string();
        info.nameId = strings_->registerString(filename);
        info.pathId = strings_->registerString(filePath);
        info.importTime = static_cast<uint64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
        
        if (analysisSink) {
            AnalysisResult res = analysisSink->getResult();
            info.peakDecibels = res.peakDecibels;
            info.rmsDecibels = res.rmsDecibels;
            info.analysis.analyzed = true;
            info.analysis.integratedLUFS = res.integratedLUFS;
            info.analysis.loudnessRange = res.loudnessRange;
            info.analysis.tempo = res.tempo;
            info.analysis.tempoConfidence = res.tempoConfidence;
            info.analysis.keyRoot = res.keyRoot;
            info.analysis.isMinor = res.isMinor;
            info.analysis.keyConfidence = res.keyConfidence;
        }

        MediaID mediaId = registry_->registerAsset(info);
        
        if (waveformSink) {
            // Use the internal cast since we know it's our implementation or just use the interface
            waveforms_->importWaveformData(mediaId, waveformSink->getAllPeaks(), waveformSink->getAllRMS());
        }
        
        if (progressCallback) progressCallback(1.0f);

        return { true, mediaId, "" };
    }

private:
    IMediaRegistry* registry_;
    Layer2::IStringRegistry* strings_;
    ICodecFactory* codecs_;
    IAudioAnalysisEngine* analysis_;
    IWaveformRenderer* waveforms_;
};

std::unique_ptr<IMediaIntakePipeline> IMediaIntakePipeline::create(
    IMediaRegistry* registry,
    Layer2::IStringRegistry* strings,
    ICodecFactory* codecs,
    IAudioAnalysisEngine* analysis,
    IWaveformRenderer* waveforms) {
    return std::make_unique<MediaIntakePipelineImpl>(registry, strings, codecs, analysis, waveforms);
}

} // namespace MediaManagement
