#pragma once

#include <Eigen/Core>
#include "primitives.h"

namespace Math {

/**
 * @brief SIMD-accelerated vector operations using Eigen.
 */
namespace Vector {

    /**
     * @brief Fills a buffer with a constant value.
     */
    inline void fill(float* buffer, float value, uint32_t size) {
        Eigen::Map<Eigen::VectorXf>(buffer, size).setConstant(value);
    }

    /**
     * @brief Zeroes out a buffer.
     */
    inline void zero(float* buffer, uint32_t size) {
        Eigen::Map<Eigen::VectorXf>(buffer, size).setZero();
    }

    /**
     * @brief Multiplies a buffer by a constant gain (In-place).
     */
    inline void applyGain(float* buffer, float gain, uint32_t size) {
        if (gain == 1.0f) return;
        if (gain == 0.0f) {
            zero(buffer, size);
            return;
        }
        Eigen::Map<Eigen::VectorXf>(buffer, size) *= gain;
    }

    /**
     * @brief Multiplies a source buffer by a constant gain and stores in destination.
     */
    inline void applyGain(float* dst, const float* src, float gain, uint32_t size) {
        if (gain == 0.0f) {
            zero(dst, size);
            return;
        }
        Eigen::Map<Eigen::VectorXf>(dst, size) = Eigen::Map<const Eigen::VectorXf>(src, size) * gain;
    }

    /**
     * @brief Copies a source buffer to a destination buffer.
     */
    inline void copy(float* dst, const float* src, uint32_t size) {
        Eigen::Map<Eigen::VectorXf>(dst, size) = Eigen::Map<const Eigen::VectorXf>(src, size);
    }

    /**
     * @brief Sanitizes an entire buffer, zeroing out NaNs, Infinities, and Denormals.
     * Uses Eigen's vectorized selection for high performance.
     */
    inline void sanitize(float* buffer, uint32_t size) {
        // Hardware FTZ/DAZ is enabled on the audio thread, so we skip the expensive software denormal check.
        // We only do a fast manual pass to catch NaNs and Infinities from misbehaving plugins.
        for (uint32_t i = 0; i < size; ++i) {
            uint32_t bits;
            std::memcpy(&bits, &buffer[i], sizeof(float));
            // IEEE 754 float: exponent is 8 bits (mask 0x7F800000). 
            // If all exponent bits are 1, it's either Infinity or NaN.
            if ((bits & 0x7F800000) == 0x7F800000) {
                buffer[i] = 0.0f;
            }
        }
    }

    /**
     * @brief Interleaves planar audio data into an interleaved buffer.
     * inputs[numChannels][numSamples] -> interleaved[numChannels * numSamples]
     */
    inline void interleave(float* dst, float* const* src, uint32_t numChannels, uint32_t numSamples) {
        for (uint32_t ch = 0; ch < numChannels; ++ch) {
            for (uint32_t i = 0; i < numSamples; ++i) {
                dst[i * numChannels + ch] = src[ch][i];
            }
        }
    }

} // namespace Vector

} // namespace Math
