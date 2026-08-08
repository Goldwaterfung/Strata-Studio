#pragma once
#include "common/system_primitives.h"
#include "common/math/analysis.h"
#include "common/math/spectral_math.h"
#include "common/dsp/realtime_telemetry.h"
#include <vector>
#include <string>
#include <cstdint>

namespace bridge {

struct MaskingBandInfo {
    std::string rangeHz;
    float maskAmountDb = 0.0f;
    std::string recommendedAction;
};

struct MaskingAnalysisResult {
    bool success = false;
    std::string errorMessage;
    uint32_t primaryTrackId = 0;
    uint32_t vsTrackId = 0;
    float overallMaskingIndex = 0.0f;
    std::string collisionRisk;
    std::vector<MaskingBandInfo> maskedBands;
};

struct ResonanceAnalysisResult {
    bool success = false;
    std::string errorMessage;
    uint32_t trackId = 0;
    std::vector<Math::Analysis::ResonancePeak> resonances;
};

struct PhaseMatrixAnalysisResult {
    bool success = false;
    std::string errorMessage;
    std::vector<uint32_t> trackIds;
    std::string globalHealth;
    uint32_t worstPairTrackA = 0;
    uint32_t worstPairTrackB = 0;
    float worstCorrelation = 1.0f;
    std::string recommendedAction;
    std::vector<float> flatMatrix;
};

struct WindowTelemetryAnalysisResult {
    bool success = false;
    std::string errorMessage;
    uint32_t trackId = 0;
    std::string startPos;
    std::string durPos;
    DSP::RealtimeTelemetryState telemetry;
    std::string safetyStatus;
    float recGainTrimDb = 0.0f;
};

struct PhaseAlignAnalysisResult {
    bool success = false;
    std::string errorMessage;
    uint32_t trackA = 0;
    uint32_t trackB = 0;
    int32_t recommendedSampleOffset = 0;
    float recommendedTimeOffsetMs = 0.0f;
    float currentCorrelation = 0.0f;
    float improvedCorrelation = 0.0f;
    std::string recommendedAction;
};

struct SpectrumAnalysisResult {
    bool success = false;
    std::string errorMessage;
    uint32_t trackId = 0;
    float spectralCentroidHz = 0.0f;
    float spectralTiltDbOct = 0.0f;
    float spectralRolloffHz = 0.0f;
    float subBandDbfs = -120.0f;
    float bassBandDbfs = -120.0f;
    float lowMidBandDbfs = -120.0f;
    float midBandDbfs = -120.0f;
    float highMidBandDbfs = -120.0f;
    float highsBandDbfs = -120.0f;
    float airBandDbfs = -120.0f;
};

struct LoudnessAnalysisResult {
    bool success = false;
    std::string errorMessage;
    uint32_t trackId = 0;
    float integratedLufs = -120.0f;
    float shortTermMaxLufs = -120.0f;
    float momentaryMaxLufs = -120.0f;
    float lraLu = 0.0f;
    float crestFactorDb = 0.0f;
    float samplePeakDbfs = -120.0f;
    float truePeakDbtp = -120.0f;
};

struct TruePeakAnalysisResult {
    bool success = false;
    std::string errorMessage;
    uint32_t trackId = 0;
    float maxTruePeakDbtp = -120.0f;
    uint32_t totalClippingEvents = 0;
    std::string safetyStatus;
};

struct StereoWidthAnalysisResult {
    bool success = false;
    std::string errorMessage;
    uint32_t trackId = 0;
    float midRmsDbfs = -120.0f;
    float sideRmsDbfs = -120.0f;
    float msRatioDb = 0.0f;
    float stereoWidthPct = 0.0f;
    float monoFoldLossDb = 0.0f;
};

/**
 * @brief Decoupled UI & Agentic Facade Interface for DSP Analysis.
 */
class IAnalysisController {
public:
    virtual ~IAnalysisController() = default;

    virtual MaskingAnalysisResult computeMasking(uint32_t primaryTrackId, uint32_t vsTrackId) = 0;
    virtual ResonanceAnalysisResult computeResonances(uint32_t trackId) = 0;
    virtual PhaseMatrixAnalysisResult computePhaseMatrix(const std::vector<uint32_t>& trackIds) = 0;
    virtual WindowTelemetryAnalysisResult getWindowTelemetry(uint32_t trackId, const std::string& startPos, const std::string& durPos) = 0;
    virtual PhaseAlignAnalysisResult computePhaseAlign(uint32_t trackA, uint32_t trackB) = 0;
    virtual SpectrumAnalysisResult computeSpectrum(uint32_t trackId) = 0;
    virtual LoudnessAnalysisResult computeLoudness(uint32_t trackId) = 0;
    virtual TruePeakAnalysisResult computeTruePeak(uint32_t trackId) = 0;
    virtual StereoWidthAnalysisResult computeStereoWidth(uint32_t trackId) = 0;
};

} // namespace bridge
