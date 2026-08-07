#pragma once
#include "ianalysis_controller.h"

namespace MediaManagement {
class IAudioAnalysisEngine;
}

namespace bridge {

class AnalysisController : public IAnalysisController {
public:
    explicit AnalysisController(MediaManagement::IAudioAnalysisEngine* analysisEngine = nullptr);

    void setAnalysisEngine(MediaManagement::IAudioAnalysisEngine* engine);

    MaskingAnalysisResult computeMasking(uint32_t primaryTrackId, uint32_t vsTrackId) override;
    ResonanceAnalysisResult computeResonances(uint32_t trackId) override;
    PhaseMatrixAnalysisResult computePhaseMatrix(const std::vector<uint32_t>& trackIds) override;
    LiveTelemetryAnalysisResult getLiveTelemetry(uint32_t trackId, uint32_t windowMs) override;
    PhaseAlignAnalysisResult computePhaseAlign(uint32_t trackA, uint32_t trackB) override;
    SpectrumAnalysisResult computeSpectrum(uint32_t trackId) override;
    LoudnessAnalysisResult computeLoudness(uint32_t trackId) override;
    TruePeakAnalysisResult computeTruePeak(uint32_t trackId) override;
    StereoWidthAnalysisResult computeStereoWidth(uint32_t trackId) override;

private:
    MediaManagement::IAudioAnalysisEngine* m_analysisEngine{nullptr};
};

} // namespace bridge
