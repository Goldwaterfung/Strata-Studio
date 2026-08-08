#pragma once

#include <Eigen/Core>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <array>

namespace Math {

/**
 * @brief Algorithms for audio signal analysis and comparison.
 */
namespace Analysis {

    struct KeyEstimate {
        uint8_t root = 0;       ///< 0 = C, 1 = C#, ..., 11 = B
        bool isMinor = false;   ///< false = Major, true = Minor
        float confidence = 0.0f;///< Normalized correlation score [0.0, 1.0]
    };

    /**
     * @brief Calculates the Root Mean Square (RMS) Error between two buffers.
     * 
     * Formula: sqrt(mean((a - b)^2))
     * A value of 0.0 indicates identity.
     * 
     * @param a Pointer to the first buffer.
     * @param b Pointer to the second buffer.
     * @param size Number of samples (not frames) to compare.
     * @return float The RMS error.
     */
    inline float calculateRMSError(const float* a, const float* b, uint32_t size) {
        if (size == 0) return 0.0f;
        
        // Use Eigen for vectorized subtraction and norm calculation
        auto vecA = Eigen::Map<const Eigen::VectorXf>(a, size);
        auto vecB = Eigen::Map<const Eigen::VectorXf>(b, size);
        
        float squaredNorm = (vecA - vecB).squaredNorm();
        return std::sqrt(squaredNorm / static_cast<float>(size));
    }

    /**
     * @brief Calculates the Pearson Correlation Coefficient between two buffers.
     * 
     * Range: [-1.0, 1.0]
     *  1.0: Perfect positive correlation (identical signal or simple gain change).
     * -1.0: Perfect negative correlation (phase inverted).
     *  0.0: No correlation (random noise vs signal).
     * 
     * @param a Pointer to the first buffer.
     * @param b Pointer to the second buffer.
     * @param size Number of samples (not frames) to compare.
     * @return float The correlation coefficient.
     */
    inline float calculateCorrelation(const float* a, const float* b, uint32_t size) {
        if (size == 0) return 0.0f;
        
        auto vecA = Eigen::Map<const Eigen::VectorXf>(a, size);
        auto vecB = Eigen::Map<const Eigen::VectorXf>(b, size);
        
        float meanA = vecA.mean();
        float meanB = vecB.mean();
        
        auto centeredA = vecA.array() - meanA;
        auto centeredB = vecB.array() - meanB;
        
        float numerator = (centeredA * centeredB).sum();
        float denominator = std::sqrt(centeredA.square().sum() * centeredB.square().sum());
        
        // Handle edge case: silence in one or both buffers
        if (denominator < 1e-9f) {
            // If both are silent (centered sums are 0), they are "correlated" in silence
            if (vecA.norm() < 1e-9f && vecB.norm() < 1e-9f) return 1.0f;
            return 0.0f;
        }
        
        return numerator / denominator;
    }

    /**
     * @brief Calculates the maximum absolute difference between two buffers.
     * 
     * Useful for strict bit-perfect checks.
     */
    inline float calculateMaxAbsoluteError(const float* a, const float* b, uint32_t size) {
        if (size == 0) return 0.0f;
        
        auto vecA = Eigen::Map<const Eigen::VectorXf>(a, size);
        auto vecB = Eigen::Map<const Eigen::VectorXf>(b, size);
        
        return (vecA - vecB).cwiseAbs().maxCoeff();
    }

    /**
     * @brief Computes Key Signature (root & mode) from a 12-bin chromagram profile using
     * Pearson correlation against Krumhansl-Schmuckler key profile templates.
     */
    inline KeyEstimate detectKeyFromChromagram(const std::array<float, 12>& chroma) {
        constexpr std::array<float, 12> kMajorProfile = {6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f};
        constexpr std::array<float, 12> kMinorProfile = {6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 2.69f, 3.34f, 3.17f, 3.28f};

        KeyEstimate bestKey{};
        float maxCorr = -2.0f;

        for (uint8_t root = 0; root < 12; ++root) {
            std::array<float, 12> rotatedChroma{};
            for (uint8_t i = 0; i < 12; ++i) {
                rotatedChroma[i] = chroma[(root + i) % 12];
            }

            float corrMaj = calculateCorrelation(rotatedChroma.data(), kMajorProfile.data(), 12);
            if (corrMaj > maxCorr) {
                maxCorr = corrMaj;
                bestKey.root = root;
                bestKey.isMinor = false;
            }

            float corrMin = calculateCorrelation(rotatedChroma.data(), kMinorProfile.data(), 12);
            if (corrMin > maxCorr) {
                maxCorr = corrMin;
                bestKey.root = root;
                bestKey.isMinor = true;
            }
        }

        bestKey.confidence = std::clamp((maxCorr + 1.0f) * 0.5f, 0.0f, 1.0f);
        return bestKey;
    }

    /**
     * @brief Result structure for narrow-Q acoustic resonance detection.
     */
    struct ResonancePeak {
        float freqHz = 0.0f;
        float prominenceDb = 0.0f;
        float qFactor = 0.0f;
        std::string notePitch;
        std::string severity;
        float recNotchDb = 0.0f;
    };

    /**
     * @brief Converts frequency in Hz to musical pitch note string (e.g. 440 Hz -> "A4").
     */
    inline std::string freqToPitchNote(float freqHz) {
        if (freqHz <= 0.0f) return "N/A";
        static const char* kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        float midiNoteFloat = 69.0f + 12.0f * std::log2(freqHz / 440.0f);
        int midiNote = static_cast<int>(std::round(midiNoteFloat));
        if (midiNote < 0 || midiNote > 127) return "N/A";
        int noteIdx = (midiNote % 12 + 12) % 12;
        int octave = (midiNote / 12) - 1;
        return std::string(kNoteNames[noteIdx]) + std::to_string(octave);
    }

    /**
     * @brief Detects narrow-Q acoustic resonances using moving median envelope filter.
     */
    inline std::vector<ResonancePeak> detectResonances(const float* magnitude,
                                                       uint32_t fftSize,
                                                       float sampleRate,
                                                       float prominenceThresholdDb = 6.0f,
                                                       float minQFactor = 8.0f) {
        std::vector<ResonancePeak> resonances;
        if (!magnitude || fftSize == 0 || sampleRate <= 0.0f) return resonances;

        const uint32_t numBins = fftSize / 2 + 1;
        const float binWidth = sampleRate / static_cast<float>(fftSize);

        // Convert magnitude spectrum to dBFS
        std::vector<float> magDb(numBins);
        for (uint32_t i = 0; i < numBins; ++i) {
            magDb[i] = 20.0f * std::log10(magnitude[i] + 1e-9f);
        }

        // Spectral envelope extraction via moving median (window = 31 bins)
        constexpr int kHalfWindow = 15;
        std::vector<float> envelopeDb(numBins);
        std::vector<float> windowBuffer;
        windowBuffer.reserve(2 * kHalfWindow + 1);

        for (uint32_t i = 0; i < numBins; ++i) {
            windowBuffer.clear();
            int start = std::max(0, static_cast<int>(i) - kHalfWindow);
            int end = std::min(static_cast<int>(numBins) - 1, static_cast<int>(i) + kHalfWindow);
            for (int j = start; j <= end; ++j) {
                windowBuffer.push_back(magDb[static_cast<size_t>(j)]);
            }
            std::sort(windowBuffer.begin(), windowBuffer.end());
            envelopeDb[i] = windowBuffer[windowBuffer.size() / 2];
        }

        // Search local peaks with prominence >= prominenceThresholdDb
        for (uint32_t i = 2; i < numBins - 2; ++i) {
            if (magDb[i] > magDb[i - 1] && magDb[i] > magDb[i + 1]) {
                float prominence = magDb[i] - envelopeDb[i];
                if (prominence >= prominenceThresholdDb) {
                    float peakFreq = i * binWidth;

                    // Find -3dB bandwidth around peak
                    float targetLevel = magDb[i] - 3.0f;
                    size_t leftBin = static_cast<size_t>(i);
                    while (leftBin > 0 && magDb[leftBin] > targetLevel) {
                        --leftBin;
                    }
                    size_t rightBin = static_cast<size_t>(i);
                    while (rightBin < (numBins - 1) && magDb[rightBin] > targetLevel) {
                        ++rightBin;
                    }

                    float bandwidthHz = std::max(static_cast<float>(rightBin - leftBin) * binWidth, binWidth);
                    float qFactor = peakFreq / bandwidthHz;

                    if (qFactor >= minQFactor) {
                        ResonancePeak peak{};
                        peak.freqHz = peakFreq;
                        peak.prominenceDb = prominence;
                        peak.qFactor = qFactor;
                        peak.notePitch = freqToPitchNote(peakFreq);

                        if (prominence >= 12.0f && qFactor >= 15.0f) {
                            peak.severity = "CRITICAL";
                            peak.recNotchDb = -6.0f;
                        } else if (prominence >= 10.0f && qFactor >= 12.0f) {
                            peak.severity = "HIGH";
                            peak.recNotchDb = -4.5f;
                        } else if (prominence >= 8.0f && qFactor >= 10.0f) {
                            peak.severity = "MODERATE";
                            peak.recNotchDb = -3.0f;
                        } else {
                            peak.severity = "LOW";
                            peak.recNotchDb = -1.5f;
                        }

                        resonances.push_back(peak);
                    }
                }
            }
        }

        return resonances;
    }

    /**
     * @brief Result structure for cross-track phase alignment.
     */
    struct PhaseAlignResult {
        int32_t recommendedSampleOffset = 0;
        float recommendedTimeOffsetMs = 0.0f;
        float currentCorrelation = 0.0f;
        float improvedCorrelation = 0.0f;
        std::string recommendedAction;
    };

    /**
     * @brief Searches for optimal time-lag sample offset between two audio buffers to maximize phase alignment.
     */
    inline PhaseAlignResult calculatePhaseAlignment(const float* a, const float* b,
                                                    uint32_t size, float sampleRate,
                                                    uint32_t maxLagSamples) {
        PhaseAlignResult result{};
        if (!a || !b || size == 0 || sampleRate <= 0.0f) {
            result.recommendedAction = "NONE";
            return result;
        }

        result.currentCorrelation = calculateCorrelation(a, b, size);
        result.improvedCorrelation = result.currentCorrelation;

        float bestScore = std::abs(result.currentCorrelation);
        int32_t bestLag = 0;
        float bestSignedCorr = result.currentCorrelation;

        uint32_t searchLimit = std::min(maxLagSamples, size / 2);

        for (int32_t lag = -static_cast<int32_t>(searchLimit); lag <= static_cast<int32_t>(searchLimit); ++lag) {
            if (lag == 0) continue;

            uint32_t absLag = static_cast<uint32_t>(std::abs(lag));
            if (absLag >= size) continue;
            uint32_t compareSize = size - absLag;
            if (compareSize == 0) continue;

            const float* ptrA = (lag >= 0) ? a : (a + absLag);
            const float* ptrB = (lag >= 0) ? (b + absLag) : b;

            float corr = calculateCorrelation(ptrA, ptrB, compareSize);
            float score = std::abs(corr);

            if (score > bestScore) {
                bestScore = score;
                bestLag = lag;
                bestSignedCorr = corr;
            }
        }

        result.recommendedSampleOffset = bestLag;
        result.recommendedTimeOffsetMs = (static_cast<float>(bestLag) / sampleRate) * 1000.0f;
        result.improvedCorrelation = bestSignedCorr;

        if (bestSignedCorr < -0.50f) {
            result.recommendedAction = "INVERT_POLARITY_TRACK_B";
        } else if (std::abs(bestLag) > 0) {
            result.recommendedAction = "DELAY_TRACK_B_BY_" + std::to_string(std::abs(bestLag)) + "_SAMPLES";
        } else {
            result.recommendedAction = "ALIGNED_NO_ACTION";
        }

        return result;
    }

    /**
     * @brief Result structure for Multi-Track Pairwise Phase Correlation Matrix.
     */
    struct PhaseMatrixResult {
        uint32_t trackCount = 0;
        std::vector<float> flatMatrix; // N x N values
        std::string globalHealth;
        uint32_t worstPairTrackA = 0;
        uint32_t worstPairTrackB = 0;
        float worstCorrelation = 1.0f;
        std::string recommendedAction;
    };

    /**
     * @brief Computes N x N pairwise phase correlation matrix across N audio buffers.
     */
    inline PhaseMatrixResult calculatePhaseCorrelationMatrix(const float* const* buffers,
                                                              uint32_t trackCount,
                                                              uint32_t sampleCount) {
        PhaseMatrixResult res{};
        res.trackCount = trackCount;
        if (!buffers || trackCount == 0 || sampleCount == 0) {
            res.globalHealth = "HEALTHY_NO_CANCELLATION";
            return res;
        }

        res.flatMatrix.assign(trackCount * trackCount, 1.0f);
        float minCorr = 1.0f;
        uint32_t worstA = 0;
        uint32_t worstB = 0;

        for (uint32_t i = 0; i < trackCount; ++i) {
            for (uint32_t j = i; j < trackCount; ++j) {
                float corr = 1.0f;
                if (i != j) {
                    if (buffers[i] && buffers[j]) {
                        corr = calculateCorrelation(buffers[i], buffers[j], sampleCount);
                    } else {
                        corr = 0.0f;
                    }
                }
                res.flatMatrix[i * trackCount + j] = corr;
                res.flatMatrix[j * trackCount + i] = corr;

                if (i != j && corr < minCorr) {
                    minCorr = corr;
                    worstA = i;
                    worstB = j;
                }
            }
        }

        res.worstPairTrackA = worstA;
        res.worstPairTrackB = worstB;
        res.worstCorrelation = minCorr;

        if (minCorr < -0.5f) {
            res.globalHealth = "WARNING_SEVERE_CANCELLATION";
            res.recommendedAction = "FLIP_POLARITY_TRACK_" + std::to_string(worstB + 1);
        } else if (minCorr < -0.2f) {
            res.globalHealth = "WARNING_MODERATE_CANCELLATION";
            res.recommendedAction = "NUDGE_DELAY_TRACK_" + std::to_string(worstB + 1) + "_PLUS_1.0MS";
        } else {
            res.globalHealth = "HEALTHY_NO_CANCELLATION";
            res.recommendedAction = "NONE";
        }

        return res;
    }

} // namespace Analysis
} // namespace Math

