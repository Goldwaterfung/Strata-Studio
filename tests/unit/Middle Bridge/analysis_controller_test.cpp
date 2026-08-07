#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "Middle Bridge/analysis/analysis_controller.h"
#include "common/math/spectral_math.h"
#include "common/math/analysis.h"
#include "common/dsp/realtime_telemetry.h"

TEST_CASE("Math::Spectral Bark Scale Conversions", "[math][spectral]") {
    SECTION("hzToBark and barkToHz basic invariants") {
        float b0 = Math::Spectral::hzToBark(0.0f);
        REQUIRE(b0 == 0.0f);

        float b1000 = Math::Spectral::hzToBark(1000.0f);
        REQUIRE(b1000 > 8.0f);
        REQUIRE(b1000 < 9.0f);

        float hzApprox = Math::Spectral::barkToHz(b1000);
        REQUIRE_THAT(hzApprox, Catch::Matchers::WithinRel(1000.0f, 0.2f));
    }

    SECTION("Bark energy integration into 24 bands") {
        constexpr uint32_t fftSize = 2048;
        constexpr uint32_t numBins = fftSize / 2 + 1;
        constexpr float sampleRate = 48000.0f;

        std::vector<float> mag(numBins, 0.0f);
        mag[10] = 1.0f; // Spike at 10 * (48000 / 2048) = ~234 Hz (Bark band ~2)

        float barkEnergy[24] = {0.0f};
        Math::Spectral::calculateBarkEnergy(mag.data(), fftSize, sampleRate, barkEnergy);

        float totalEnergy = 0.0f;
        for (int i = 0; i < 24; ++i) totalEnergy += barkEnergy[i];
        REQUIRE(totalEnergy > 0.0f);
    }
}

TEST_CASE("Math::Analysis Resonance Detection", "[math][analysis]") {
    SECTION("detectResonances identifies synthetic narrow peaks") {
        constexpr uint32_t fftSize = 4096;
        constexpr uint32_t numBins = fftSize / 2 + 1;
        constexpr float sampleRate = 48000.0f;

        std::vector<float> mag(numBins, 0.001f);
        uint32_t bin315 = static_cast<uint32_t>(315.4f / (sampleRate / fftSize));
        if (bin315 < numBins) {
            mag[bin315] = 0.6f;
        }

        auto peaks = Math::Analysis::detectResonances(mag.data(), fftSize, sampleRate, 6.0f, 8.0f);
        REQUIRE_FALSE(peaks.empty());
        REQUIRE_THAT(peaks[0].freqHz, Catch::Matchers::WithinRel(315.4f, 0.1f));
        REQUIRE(peaks[0].qFactor >= 8.0f);
    }
}

TEST_CASE("Math::Analysis Multi-Track Phase Matrix", "[math][analysis]") {
    SECTION("calculatePhaseCorrelationMatrix computes identical vs inverted pairs") {
        constexpr uint32_t numSamples = 512;
        std::vector<float> sigA(numSamples);
        std::vector<float> sigB(numSamples);
        std::vector<float> sigC(numSamples);

        for (uint32_t i = 0; i < numSamples; ++i) {
            float v = std::sin(2.0f * 3.14159f * 100.0f * (i / 48000.0f));
            sigA[i] = v;
            sigB[i] = v;       // identical
            sigC[i] = -v;      // phase inverted
        }

        const float* bufs[3] = { sigA.data(), sigB.data(), sigC.data() };
        auto res = Math::Analysis::calculatePhaseCorrelationMatrix(bufs, 3, numSamples);

        REQUIRE(res.trackCount == 3);
        REQUIRE_THAT(res.flatMatrix[0 * 3 + 1], Catch::Matchers::WithinRel(1.0f, 0.01f));
        REQUIRE_THAT(res.flatMatrix[0 * 3 + 2], Catch::Matchers::WithinRel(-1.0f, 0.01f));
        REQUIRE(res.globalHealth == "WARNING_SEVERE_CANCELLATION");
    }
}

TEST_CASE("DSP::RealtimeTelemetry State Accumulator", "[dsp][telemetry]") {
    SECTION("accumulateBlockTelemetry accurately computes peak and RMS") {
        constexpr uint32_t numSamples = 256;
        std::vector<float> ch1(numSamples, 0.5f);
        const float* bufs[1] = { ch1.data() };

        DSP::RealtimeTelemetryState state{};
        DSP::accumulateBlockTelemetry(bufs, 1, numSamples, state);

        REQUIRE_THAT(state.peakDbfs, Catch::Matchers::WithinRel(-6.02f, 0.05f));
        REQUIRE_THAT(state.rmsDbfs, Catch::Matchers::WithinRel(-6.02f, 0.05f));
        REQUIRE_FALSE(state.isClipping);
    }
}

TEST_CASE("bridge::AnalysisController Execution", "[bridge][analysis]") {
    bridge::AnalysisController controller;

    SECTION("computeMasking returns calculated collision index") {
        auto res = controller.computeMasking(1, 2);
        REQUIRE(res.success);
        REQUIRE(res.primaryTrackId == 1);
        REQUIRE(res.vsTrackId == 2);
        REQUIRE(res.overallMaskingIndex >= 0.0f);
        REQUIRE(res.overallMaskingIndex <= 1.0f);
        REQUIRE_FALSE(res.maskedBands.empty());
    }

    SECTION("computeResonances returns detected peaks") {
        auto res = controller.computeResonances(1);
        REQUIRE(res.success);
        REQUIRE(res.trackId == 1);
        REQUIRE_FALSE(res.resonances.empty());
    }

    SECTION("computePhaseMatrix computes N x N matrix") {
        std::vector<uint32_t> tracks = {1, 2, 3, 4};
        auto res = controller.computePhaseMatrix(tracks);
        REQUIRE(res.success);
        REQUIRE(res.trackIds.size() == 4);
        REQUIRE(res.flatMatrix.size() == 16);
    }

    SECTION("computePhaseAlign computes optimal lag offset") {
        auto res = controller.computePhaseAlign(1, 2);
        REQUIRE(res.success);
        REQUIRE(res.improvedCorrelation >= res.currentCorrelation);
    }

    SECTION("getLiveTelemetry returns telemetry snapshot") {
        auto res = controller.getLiveTelemetry(1, 400);
        REQUIRE(res.success);
        REQUIRE(res.trackId == 1);
        REQUIRE(res.windowMs == 400);
    }

    SECTION("computeSpectrum computes centroid and frequency bands") {
        auto res = controller.computeSpectrum(1);
        REQUIRE(res.success);
        REQUIRE(res.trackId == 1);
        REQUIRE(res.spectralCentroidHz > 0.0f);
    }

    SECTION("computeLoudness computes telemetry metrics") {
        auto res = controller.computeLoudness(1);
        REQUIRE(res.success);
        REQUIRE(res.trackId == 1);
    }

    SECTION("computeTruePeak computes clipping stats") {
        auto res = controller.computeTruePeak(1);
        REQUIRE(res.success);
        REQUIRE(res.trackId == 1);
    }

    SECTION("computeStereoWidth computes M/S ratio") {
        auto res = controller.computeStereoWidth(1);
        REQUIRE(res.success);
        REQUIRE(res.trackId == 1);
    }
}
