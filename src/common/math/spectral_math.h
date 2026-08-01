#pragma once

#include <vector>
#include <complex>
#include <cmath>
#include <Eigen/Core>
#include <unsupported/Eigen/FFT>
#include "primitives.h"

namespace Math {

/**
 * @brief Utilities for frequency-domain (spectral) processing.
 */
namespace Spectral {

    /**
     * @brief Window functions for FFT processing.
     */
    enum class WindowType {
        Rectangular,
        Hann,
        Hamming,
        Blackman,
        FlatTop
    };

    /**
     * @brief Generates a window of a specific type and size.
     */
    inline void generateWindow(float* window, uint32_t size, WindowType type) {
        if (!window || size == 0) return;

        const float n_minus_1 = static_cast<float>(size - 1);
        const float two_pi = 2.0f * Constants::PI;

        for (uint32_t i = 0; i < size; ++i) {
            float phase = static_cast<float>(i) / n_minus_1;
            
            switch (type) {
                case WindowType::Rectangular:
                    window[i] = 1.0f;
                    break;
                case WindowType::Hann:
                    window[i] = 0.5f * (1.0f - std::cos(two_pi * phase));
                    break;
                case WindowType::Hamming:
                    window[i] = 0.54f - 0.46f * std::cos(two_pi * phase);
                    break;
                case WindowType::Blackman:
                    window[i] = 0.42f - 0.5f * std::cos(two_pi * phase) + 0.08f * std::cos(2.0f * two_pi * phase);
                    break;
                case WindowType::FlatTop:
                    window[i] = 0.21557895f - 0.41663158f * std::cos(two_pi * phase) + 
                                0.277263158f * std::cos(2.0f * two_pi * phase) - 
                                0.083578947f * std::cos(3.0f * two_pi * phase) + 
                                0.006947368f * std::cos(4.0f * two_pi * phase);
                    break;
            }
        }
    }

    /**
     * @brief Applies a window to a buffer of samples.
     */
    inline void applyWindow(float* data, const float* window, uint32_t size) {
        if (!data || !window) return;
        for (uint32_t i = 0; i < size; ++i) {
            data[i] *= window[i];
        }
    }

    /**
     * @brief Wrapper for Eigen's FFT implementation.
     */
    class FFTProcessor {
    public:
        FFTProcessor() = default;

        /**
         * @brief Performs a forward FFT (Time to Frequency).
         * @param timeDomain Input time-domain samples (real).
         * @param freqDomain Output frequency-domain samples (complex).
         * @param size Size of the transform (must be power of 2 for best performance).
         */
        void forward(const float* timeDomain, std::complex<float>* freqDomain, uint32_t size) {
            _fft.fwd(freqDomain, timeDomain, static_cast<Eigen::Index>(size));
        }

        /**
         * @brief Calculates the magnitude spectrum from complex data.
         */
        static void calculateMagnitude(const std::complex<float>* freqDomain, float* magnitude, uint32_t size) {
            for (uint32_t i = 0; i < size; ++i) {
                magnitude[i] = std::abs(freqDomain[i]);
            }
        }

    private:
        Eigen::FFT<float> _fft;
    };

} // namespace Spectral

} // namespace Math
