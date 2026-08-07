#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>
#include <array>

namespace DSP {

/**
 * @brief Real-time telemetry snapshot structure (POD).
 */
struct RealtimeTelemetryState {
    float peakDbfs = -120.0f;
    float truePeakDbtp = -120.0f;
    float rmsDbfs = -120.0f;
    float momentaryLufs = -120.0f;
    float shortTermLufs = -120.0f;
    float crestFactorDb = 0.0f;
    uint32_t clipEventsCount = 0;
    bool isClipping = false;
    float spectralCentroidHz = 0.0f;
    float stereoCorrelation = 1.0f;
};

/**
 * @brief Accumulates block telemetry metrics in an O(N) non-allocating RT loop.
 */
inline void accumulateBlockTelemetry(const float* const* channelBuffers,
                                      uint32_t numChannels,
                                      uint32_t numSamples,
                                      RealtimeTelemetryState& outState) {
    if (!channelBuffers || numChannels == 0 || numSamples == 0) return;

    float maxAbs = 0.0f;
    float sumSq = 0.0f;
    uint32_t totalSamples = numChannels * numSamples;

    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        const float* buf = channelBuffers[ch];
        if (!buf) continue;

        for (uint32_t i = 0; i < numSamples; ++i) {
            float absVal = std::abs(buf[i]);
            if (absVal > maxAbs) {
                maxAbs = absVal;
            }
            if (absVal >= 1.0f) {
                outState.clipEventsCount++;
                outState.isClipping = true;
            }
            sumSq += buf[i] * buf[i];
        }
    }

    float rmsLinear = std::sqrt(sumSq / static_cast<float>(std::max(1u, totalSamples)));
    outState.peakDbfs = 20.0f * std::log10(maxAbs + 1e-9f);
    outState.truePeakDbtp = 20.0f * std::log10(maxAbs * 1.05f + 1e-9f); // Approximated 4x oversampled peak
    outState.rmsDbfs = 20.0f * std::log10(rmsLinear + 1e-9f);

    float peakLinear = std::max(maxAbs, 1e-6f);
    float rmsNonZero = std::max(rmsLinear, 1e-6f);
    outState.crestFactorDb = 20.0f * std::log10(peakLinear / rmsNonZero);
}

/**
 * @brief 400ms pre-allocated circular ring buffer for live momentary LUFS computation.
 */
template <uint32_t Capacity = 19200>
class SlidingWindowLoudnessBuffer {
public:
    SlidingWindowLoudnessBuffer() { reset(); }

    void reset() {
        m_buffer.fill(0.0f);
        m_writeIdx = 0;
        m_sumSq = 0.0f;
    }

    void pushBlock(const float* data, uint32_t numSamples) {
        if (!data || numSamples == 0) return;
        for (uint32_t i = 0; i < numSamples; ++i) {
            float sample = data[i];
            float oldSample = m_buffer[m_writeIdx];
            m_sumSq -= oldSample * oldSample;
            if (m_sumSq < 0.0f) m_sumSq = 0.0f;

            m_buffer[m_writeIdx] = sample;
            m_sumSq += sample * sample;
            m_writeIdx = (m_writeIdx + 1) % Capacity;
        }
    }

    float getMomentaryLufs() const {
        float meanSq = m_sumSq / static_cast<float>(Capacity);
        float rms = std::sqrt(meanSq);
        // EBU R128 momentary loudness approximation from K-weighted energy
        float lufs = 20.0f * std::log10(rms + 1e-9f) - 0.691f;
        return std::clamp(lufs, -120.0f, 10.0f);
    }

private:
    std::array<float, Capacity> m_buffer;
    uint32_t m_writeIdx = 0;
    float m_sumSq = 0.0f;
};

} // namespace DSP
