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

    /**
     * @brief Converts frequency in Hz to Bark scale.
     * Formula: z(f) = 13 * arctan(0.00076 * f) + 3.5 * arctan((f / 7500)^2)
     */
    inline float hzToBark(float freqHz) {
        if (freqHz <= 0.0f) return 0.0f;
        float term1 = 13.0f * std::atan(0.00076f * freqHz);
        float ratio = freqHz / 7500.0f;
        float term2 = 3.5f * std::atan(ratio * ratio);
        return term1 + term2;
    }

    /**
     * @brief Converts Bark scale value back to frequency in Hz.
     */
    inline float barkToHz(float bark) {
        if (bark <= 0.0f) return 0.0f;
        // Inverse approximation: f = 600 * sinh(z / 6)
        return 600.0f * std::sinh(bark / 6.0f);
    }

    /**
     * @brief Integrates FFT magnitude spectrum into 24 Bark critical bands.
     * @param magnitude Magnitude spectrum array of size (fftSize / 2 + 1)
     * @param fftSize FFT frame size (e.g. 2048, 4096)
     * @param sampleRate Sampling rate in Hz (e.g. 48000.0f)
     * @param barkEnergy24 Output array of size 24 for Bark band energies
     */
    inline void calculateBarkEnergy(const float* magnitude, uint32_t fftSize, float sampleRate, float* barkEnergy24) {
        if (!magnitude || !barkEnergy24 || fftSize == 0 || sampleRate <= 0.0f) return;

        for (uint32_t b = 0; b < 24; ++b) {
            barkEnergy24[b] = 0.0f;
        }

        const uint32_t numBins = fftSize / 2 + 1;
        const float binWidth = sampleRate / static_cast<float>(fftSize);

        for (uint32_t bin = 0; bin < numBins; ++bin) {
            float freqHz = bin * binWidth;
            float bark = hzToBark(freqHz);
            uint32_t band = static_cast<uint32_t>(std::clamp(std::floor(bark), 0.0f, 23.0f));
            float mag = magnitude[bin];
            barkEnergy24[band] += mag * mag; // Energy density
        }
    }

    /**
     * @brief Calculates Schroeder spreading function S_dB(delta_z) in dB.
     * S_dB(dz) = 15.81 + 7.5*(dz + 0.474) - 17.5 * sqrt(1 + (dz + 0.474)^2)
     */
    inline float schroederSpreadingDb(float deltaBark) {
        float dz = deltaBark + 0.474f;
        return 15.81f + 7.5f * dz - 17.5f * std::sqrt(1.0f + dz * dz);
    }

    /**
     * @brief Computes psychoacoustic masking threshold curve M_A(z) across 24 Bark bands.
     * @param barkEnergyA Energy density of masker signal across 24 Bark bands.
     * @param maskingThresholdOut Output array of size 24 for masking energy thresholds.
     */
    inline void calculateMaskingThreshold(const float* barkEnergyA, float* maskingThresholdOut) {
        if (!barkEnergyA || !maskingThresholdOut) return;

        // Absolute threshold of hearing T_q(z) in dB SPL per Bark band (approximate)
        static const float kAbsoluteThresholdDb[24] = {
            30.0f, 18.0f, 12.0f, 8.0f,  6.0f,  4.0f,  3.0f,  2.0f,
            2.0f,  2.0f,  2.0f,  3.0f,  4.0f,  5.0f,  7.0f,  9.0f,
            12.0f, 16.0f, 21.0f, 27.0f, 34.0f, 42.0f, 52.0f, 65.0f
        };

        for (uint32_t z = 0; z < 24; ++z) {
            float z_val = static_cast<float>(z + 1); // 1-based Bark band index
            float sumMaskingEnergy = std::pow(10.0f, kAbsoluteThresholdDb[z] / 10.0f);

            for (uint32_t zm = 0; zm < 24; ++zm) {
                float energyZm = barkEnergyA[zm];
                if (energyZm <= 1e-12f) continue;

                float energyDb = 10.0f * std::log10(energyZm + 1e-12f);
                float offsetDb = 14.5f + static_cast<float>(zm + 1); // Tone vs noise offset
                float deltaZ = z_val - static_cast<float>(zm + 1);
                float spreadDb = schroederSpreadingDb(deltaZ);

                float maskedLevelDb = energyDb - offsetDb + spreadDb;
                sumMaskingEnergy += std::pow(10.0f, maskedLevelDb / 10.0f);
            }

            maskingThresholdOut[z] = sumMaskingEnergy;
        }
    }

    /**
     * @brief Computes overall masking collision index C_A,B in [0.0, 1.0] and per-band dB masking.
     */
    inline float calculateMaskingCollisionIndex(const float* barkEnergyB,
                                                 const float* maskingThresholdA,
                                                 float* outPerBandMaskDb = nullptr) {
        if (!barkEnergyB || !maskingThresholdA) return 0.0f;

        float totalEnergyB = 0.0f;
        float totalCollidedEnergy = 0.0f;

        for (uint32_t z = 0; z < 24; ++z) {
            float eB = barkEnergyB[z];
            float mA = maskingThresholdA[z];
            totalEnergyB += eB;

            float collided = std::min(eB, mA);
            totalCollidedEnergy += collided;

            if (outPerBandMaskDb) {
                if (eB > 1e-12f && mA > 1e-12f) {
                    float ratio = std::min(eB, mA) / eB;
                    outPerBandMaskDb[z] = 10.0f * std::log10(ratio + 1e-12f);
                } else {
                    outPerBandMaskDb[z] = 0.0f;
                }
            }
        }

        if (totalEnergyB < 1e-12f) return 0.0f;
        return std::clamp(totalCollidedEnergy / totalEnergyB, 0.0f, 1.0f);
    }

} // namespace Spectral

} // namespace Math

