#include "bpm_analyzer.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wold-style-cast"
#include <BPMDetect.h>
#pragma clang diagnostic pop
#include <vector>
#include <algorithm>

namespace MediaManagement {

using namespace soundtouch;

BPMAnalyzer::Result BPMAnalyzer::analyze(
    MediaID sourceId,
    ICodecReader* reader,
    ProgressCallback progressCallback,
    void* context
) {
    Result result;
    result.sourceId = sourceId;

    if (!reader || reader->getTotalFrames() == 0) {
        return result;
    }

    uint32_t sampleRate = reader->getSampleRate();
    uint16_t numChannels = reader->getNumChannels();
    
    // SoundTouch BPMDetect handles interleaved float samples
    BPMDetect detector(static_cast<int>(numChannels), static_cast<int>(sampleRate));

    // Chunk size for streaming - matches SoundStretch logic (approx 0.15s at 44.1k)
    const uint32_t CHUNK_SIZE = 6720;
    std::vector<float> buffer(CHUNK_SIZE * numChannels);

    uint64_t totalFrames = reader->getTotalFrames();
    uint64_t framesProcessed = 0;

    while (framesProcessed < totalFrames) {
        uint32_t toRead = static_cast<uint32_t>(std::min(static_cast<uint64_t>(CHUNK_SIZE), totalFrames - framesProcessed));
        uint32_t read = reader->readFrames(buffer.data(), toRead);
        
        if (read == 0) break;

        // Feed the interleaved samples to the detector
        // BPMDetect performs internal decimation (~1000Hz) and autocorrelation
        detector.inputSamples(buffer.data(), static_cast<int>(read));

        framesProcessed += read;

        if (progressCallback) {
            progressCallback(context, static_cast<float>(framesProcessed) / static_cast<float>(totalFrames));
        }
    }

    // Retrieve results
    result.bpm = detector.getBpm();
    
    // Simple confidence estimation based on beat detection strength
    // SoundTouch doesn't provide a direct confidence, but we can assume success if bpm > 0
    if (result.bpm > 0.0f) {
        result.confidence = 0.8f; // Baseline confidence for a successful detection
    }

    return result;
}

} // namespace MediaManagement
