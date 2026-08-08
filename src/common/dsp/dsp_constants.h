#pragma once

#include <cstdint>

namespace DSP {
namespace Constants {

    // General Analysis Constants
    constexpr uint32_t kDefaultFFTSize = 2048;
    constexpr uint32_t kHighResFFTSize = 4096;
    constexpr uint32_t kPhaseMatrixSamples = 1024;
    constexpr uint32_t kPhaseAlignSamples = 2048;
    
    // Masking Analysis
    constexpr float kMaskingHighRiskThreshold = 0.70f;
    constexpr float kMaskingModerateRiskThreshold = 0.40f;
    
    // Resonance Search
    constexpr float kResonanceProminenceThreshold = 6.0f;
    constexpr float kResonanceQFactorThreshold = 8.0f;
    
    // Spectral Analysis
    constexpr float kSpectralRolloffTarget = 0.85f; // 85% energy
    
    // Telemetry Targets
    constexpr float kSafetyHeadroomDb = -1.0f;
    constexpr float kBS1770CalibrationGain = -0.691f;

    // Phase Alignment & Correlation
    constexpr int32_t kPhaseAlignMaxOffsetSamples = 128;
    constexpr float kMaxPhaseLagSeconds = 0.050f; // 50ms maximum search window
    constexpr float kPolarityInvertCorrelationThreshold = 0.50f;
    constexpr float kModerateCancellationThreshold = 0.20f;
    constexpr float kSilenceFloorDbfs = -120.0f;
    constexpr uint32_t kOfflineAnalysisPollingIntervalMs = 20;
    constexpr uint32_t kDefaultAnalysisBlockSize = 1024;

} // namespace Constants
} // namespace DSP
