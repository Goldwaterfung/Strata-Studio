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

} // namespace Analysis
} // namespace Math
