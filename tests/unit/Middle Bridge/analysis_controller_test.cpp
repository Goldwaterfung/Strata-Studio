#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "Middle Bridge/analysis/analysis_controller.h"
#include "Middle Bridge/telemetry/imetering_provider.h"
#include "Middle Bridge/engine/ihardware_settings_facade.h"
#include "Middle Bridge/engine/irender_controller.h"
#include "Middle Bridge/project/iproject_lifecycle_controller.h"
#include "Middle Bridge/timeline/iarrangement_controller.h"
#include "Middle Bridge/timeline/itimeline_controller.h"
#include "Media management/analysis/iaudio_analysis_engine.h"
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

class MockMeteringProvider : public bridge::IMeteringProvider {
public:
    bridge::MeterLevel getTrackLevels(TrackID) override {
        bridge::MeterLevel lvl{};
        lvl.peakLeft = -0.5f;
        lvl.peakRight = -0.5f;
        lvl.rmsLeft = -10.0f;
        lvl.rmsRight = -10.0f;
        lvl.clipLeft = false;
        lvl.clipRight = false;
        return lvl;
    }
    void resetTrackClip(TrackID) override {}
    bridge::MeterLevel getMasterLevels() override {
        bridge::MeterLevel lvl{};
        lvl.peakLeft = -0.2f;
        lvl.peakRight = -0.2f;
        lvl.rmsLeft = -8.0f;
        lvl.rmsRight = -8.0f;
        return lvl;
    }
    void resetMasterClip() override {}
    void registerTrackNodeMapping(TrackID, NodeID) override {}
    void unregisterTrackNodeMapping(TrackID) override {}
    void updateMeters(double) override {}
    void getSpectrumData(NodeID, float* outMagnitudes, uint32_t binCount) override {
        for (uint32_t i = 0; i < binCount; ++i) {
            outMagnitudes[i] = 0.001f;
        }
        if (binCount > 20) {
            outMagnitudes[20] = 0.8f;
        }
    }
};

class MockHardwareSettings : public bridge::IHardwareSettingsFacade {
public:
    std::vector<bridge::AudioDeviceDescriptor> getAvailableDevices() override { return {}; }
    bridge::HardwareConfig getCurrentConfig() override {
        bridge::HardwareConfig c{};
        c.sampleRate = 48000;
        return c;
    }
    bool applyConfig(const bridge::HardwareConfig&) override { return true; }
    double getCpuLoad() override { return 0.0; }
    double getLatencyMs() override { return 5.0; }
    uint32_t getXrunCount() override { return 0; }
    std::vector<bridge::MidiPortDescriptor> getAvailableMidiPorts() override { return {}; }
    bool isMidiPortOpen(uint32_t) override { return false; }
    bool setMidiPortOpen(uint32_t, bool) override { return true; }
};

class MockRenderController : public bridge::IRenderController {
public:
    void startOfflineRender(const RenderConfiguration&) override {}
    bool isRenderingActive() const override { return false; }
    float getRenderProgress() const override { return 1.0f; }
    const char* getRenderStatusMessage() const override { return "Completed"; }
    void cancelOfflineRender() override {}
    bool hasFailed(char*, uint32_t) const override { return false; }
    void startSilentMixAnalysis(uint64_t, uint64_t, uint32_t, uint32_t) override {}
    bool renderTrackToBufferSync(uint32_t, uint64_t, uint64_t, uint32_t, std::vector<float>& out) override {
        out.assign(1024, 0.5f);
        return true;
    }
};

class MockLifecycleController : public bridge::IProjectLifecycleController {
public:
    bool createNewProject(const bridge::ProjectMetadataState&) override { return true; }
    bool loadProject(const char*) override { return true; }
    bool saveProject(const char* = "") override { return true; }
    bool exportProjectToJson(const char*) override { return true; }
    bool importProjectFromJson(const char*) override { return true; }
    void closeProject() override {}
    bool hasActiveProject() const override { return true; }
    bool isProjectDirty() const override { return false; }
    std::string getCurrentProjectPath() const override { return "/test/project.daw"; }
    bridge::ProjectMetadataState getCurrentProjectMetadata() const override { return {}; }
    std::vector<composition::MissingPluginReport> getMissingPluginsFromLastLoad() const override { return {}; }
    bool isOperationPending() const override { return false; }
    float getOperationProgress() const override { return 1.0f; }

    bridge::MixStatisticsState getMixStatisticsState() const override {
        bridge::MixStatisticsState s{};
        s.isAnalyzed = true;
        s.integratedLoudnessLUFS = -14.0f;
        s.truePeakDBTP = -1.0f;
        s.samplePeakDBFS = -0.5f;
        s.midRmsDbfs = -12.0f;
        s.sideRmsDbfs = -18.0f;
        s.msRatioDb = 6.0f;
        s.stereoWidthPct = 35.0f;
        s.monoFoldLossDb = -0.2f;
        s.stereoCorrelation = 0.85f;
        return s;
    }
};

class MockArrangementController : public bridge::IArrangementController {
public:
    composition::RegionID importAudioClip(TrackID, const char*, uint64_t) override { return {}; }
    composition::RegionID insertMidiClip(TrackID, uint64_t, uint64_t) override { return {}; }
    composition::RegionID insertAutomationClip(TrackID, NodeID, uint32_t, uint64_t, uint64_t) override { return {}; }
    void deleteRegion(composition::RegionID) override {}
    void splitRegion(composition::RegionID, uint64_t) override {}
    composition::RegionID moveRegion(composition::RegionID, TrackID, int64_t, uint32_t) override { return {}; }
    void trimRegion(composition::RegionID, uint64_t, uint64_t, uint64_t) override {}
    bool getVisualRegion(composition::RegionID, bridge::VisualRegion&) const override { return false; }
    void setRegionGain(composition::RegionID, float) override {}
    void setRegionFades(composition::RegionID, uint32_t, uint32_t) override {}
    void setRegionMuted(composition::RegionID, bool) override {}
    void setRegionWarpMode(composition::RegionID, WarpMode) override {}
    void setRegionPlaybackRatio(composition::RegionID, float) override {}
    void setRegionSourceBpm(composition::RegionID, float) override {}
    void updateRegionMetadata(composition::RegionID, const char*, const char*, uint32_t) override {}
    void initializeRegionMetadata(composition::RegionID, const char*, uint32_t) override {}
    void mergePatternClips(TrackID, uint64_t, uint64_t) override {}
    void swapTakeLayer(TrackID, uint32_t, uint64_t, uint64_t) override {}
    void autoNameClips(TrackID) override {}
    bool undo() override { return true; }
    bool redo() override { return true; }
    void consolidateTrack(TrackID, uint64_t, uint64_t) override {}
    uint64_t getArrangementLength() const override { return 48000 * 10; }
    uint32_t getRegionsInViewport(uint64_t, uint64_t, bridge::VisualRegion*, uint32_t) const override { return 0; }
    uint32_t getActiveRecordings(bridge::IArrangementController::VisualActiveRecording*, uint32_t) override { return 0; }
};

class MockTimelineController : public bridge::ITimelineController {
public:
    void togglePlay() override {}
    void play() override {}
    void stop() override {}
    void setRecordArmed(bool) override {}
    void seekToFrame(uint64_t) override {}
    void seekToTimeSeconds(double) override {}
    void seekToMusicalGrid(double, double) override {}
    void setBPM(double) override {}
    void setTimeSignature(bridge::VisualTimeSignature) override {}
    void setLoopRange(uint64_t, uint64_t) override {}
    void setLoopEnabled(bool) override {}
    bool isTempoAutomated() const override { return false; }
    void removeTempoPoint(uint64_t) override {}
    void addTempoPoint(uint64_t, double) override {}
    uint32_t getTempoPoints(uint64_t, uint64_t, bridge::VisualTempoPoint*, uint32_t) const override { return 0; }
    uint64_t getCurrentFrame() const override { return 0; }
    double getCurrentSeconds() const override { return 0.0; }
    bool isPlaying() const override { return false; }
    bool isRecording() const override { return false; }
    bool isRecordArmed() const override { return false; }
    bool isLooping() const override { return false; }
    double getBPM() const override { return 120.0; }
    void addMarker(uint64_t, const char*, uint32_t) override {}
    void removeMarker(const MarkerUUID&) override {}
    void updateMarker(const MarkerUUID&, uint64_t, const char*, uint32_t) override {}
    uint32_t getMarkersInRange(uint64_t, uint64_t, bridge::VisualMarker*, uint32_t) const override { return 0; }
    double pixelsToFrames(float, float) const override { return 0.0; }
    float framesToPixels(uint64_t, float) const override { return 0.0f; }
    double getSampleRate() const override { return 48000.0; }
    uint64_t getLoopStart() const override { return 0; }
    uint64_t getLoopEnd() const override { return 0; }
    void setPlaybackMode(PlaybackMode) override {}
    PlaybackMode getPlaybackMode() const override { return PlaybackMode::SONG; }
    void getCurrentBBT(uint32_t& b, uint32_t& be, uint32_t& t) const override { b = 1; be = 1; t = 0; }
    uint64_t samplesToTicks(uint64_t s) const override { return s; }
    uint64_t ticksToSamples(uint64_t t) const override { return t; }
    uint32_t getTicksPerBeat() const override { return 960; }
    void frameToBBT(uint64_t, uint32_t& b, uint32_t& be, uint32_t& t) const override { b = 1; be = 1; t = 0; }
    uint64_t bbtToFrame(uint32_t, uint32_t, uint32_t) const override { return 48000; }
    void getTimeSignatureAtFrame(uint64_t, uint8_t& n, uint8_t& d) const override { n = 4; d = 4; }
    void setMetronomeEnabled(bool) override {}
    bool isMetronomeEnabled() const override { return false; }
    void setCountInEnabled(bool) override {}
    bool isCountInEnabled() const override { return false; }
    void setCountInBars(uint8_t) override {}
    uint8_t getCountInBars() const override { return 1; }
};

class MockAnalysisEngine : public MediaManagement::IAudioAnalysisEngine {
public:
    void analyzeRealtime(const struct AudioBuffer*, uint32_t, MediaManagement::AnalysisResult&) override {}
    bool analyze(MediaID, MediaManagement::AnalysisResult&) override { return true; }
    bool analyzeAsync(MediaID, CompletionCallback, void*) override { return true; }
    void getSpectralFluxData(MediaID, float* buf, uint32_t size) override {
        for (uint32_t i = 0; i < size; ++i) buf[i] = 0.001f;
        if (size > 20) buf[20] = 0.8f;
    }
    void getTransientData(MediaID, uint64_t*, float*, uint32_t) override {}
    void getPitchData(MediaID, float*, uint32_t) override {}
    void calculateLoudness(MediaID, float*, float*, float*) override {}
    void detectTempo(MediaID, float*, float*) override {}
    void detectKey(MediaID, uint8_t*, bool*, float*) override {}
    std::unique_ptr<MediaManagement::IAnalysisSink> createSink(uint32_t, uint16_t) override { return nullptr; }
    void update() override {}
};

TEST_CASE("bridge::AnalysisController Execution", "[bridge][analysis]") {
    bridge::AnalysisController controller;
    MockMeteringProvider mockMetering;
    MockHardwareSettings mockHardware;
    MockRenderController mockRender;
    MockLifecycleController mockLifecycle;
    MockArrangementController mockArrangement;
    MockTimelineController mockTimeline;
    MockAnalysisEngine mockAnalysisEngine;

    controller.setMeteringProvider(&mockMetering);
    controller.setHardwareSettings(&mockHardware);
    controller.setRenderController(&mockRender);
    controller.setLifecycleController(&mockLifecycle);
    controller.setArrangementController(&mockArrangement);
    controller.setTimelineController(&mockTimeline);
    controller.setAnalysisEngine(&mockAnalysisEngine);

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

    SECTION("getWindowTelemetry returns telemetry snapshot") {
        auto res = controller.getWindowTelemetry(1, "00:00:00", "00:00:05");
        REQUIRE(res.trackId == 1);
        REQUIRE(res.startPos == "00:00:00");
        REQUIRE(res.durPos == "00:00:05");
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
        REQUIRE(res.stereoWidthPct >= 0.0f);
    }

    SECTION("Synthetic Ground Truth: EBU R128 Loudness and True Peak") {
        constexpr uint32_t numSamples = 48000; // 1 second @ 48kHz
        std::vector<float> sineWave(numSamples);
        for (uint32_t i = 0; i < numSamples; ++i) {
            // 1 kHz sine at 0 dBFS (amplitude = 1.0)
            sineWave[i] = std::sin(2.0f * 3.14159265f * 1000.0f * (static_cast<float>(i) / 48000.0f));
        }

        const float* bufs[1] = { sineWave.data() };
        DSP::RealtimeTelemetryState state{};
        DSP::accumulateBlockTelemetry(bufs, 1, numSamples, state);

        // Sine at 0 dBFS has RMS = 1/sqrt(2) = -3.01 dBFS
        REQUIRE_THAT(state.rmsDbfs, Catch::Matchers::WithinRel(-3.01f, 0.05f));
        REQUIRE_THAT(state.peakDbfs, Catch::Matchers::WithinRel(0.0f, 0.05f));
    }

    SECTION("Synthetic Ground Truth: Exact Sample Lag Offset Sweep") {
        constexpr uint32_t numSamples = 2048;
        constexpr float sampleRate = 48000.0f;
        std::vector<float> sigA(numSamples, 0.0f);
        std::vector<float> sigB(numSamples, 0.0f);

        constexpr int32_t kKnownDelay = 48; // Exactly 1.0 ms @ 48kHz
        for (uint32_t i = 0; i < numSamples; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            sigA[i] = std::sin(2.0f * 3.14159265f * 200.0f * t)
                    + 0.5f * std::sin(2.0f * 3.14159265f * 500.0f * t)
                    + 0.25f * std::sin(2.0f * 3.14159265f * 1200.0f * t);
            if (static_cast<int32_t>(i) >= kKnownDelay) {
                float tDelayed = t - (static_cast<float>(kKnownDelay) / sampleRate);
                sigB[i] = std::sin(2.0f * 3.14159265f * 200.0f * tDelayed)
                        + 0.5f * std::sin(2.0f * 3.14159265f * 500.0f * tDelayed)
                        + 0.25f * std::sin(2.0f * 3.14159265f * 1200.0f * tDelayed);
            }
        }

        float maxCorr = -2.0f;
        int32_t bestOffset = 0;
        for (int32_t offset = -128; offset <= 128; ++offset) {
            std::vector<float> shiftedB(numSamples, 0.0f);
            for (uint32_t i = 0; i < numSamples; ++i) {
                int srcIdx = static_cast<int>(i) - offset;
                if (srcIdx >= 0 && srcIdx < static_cast<int>(numSamples)) {
                    shiftedB[i] = sigB[static_cast<size_t>(srcIdx)];
                }
            }
            float corr = Math::Analysis::calculateCorrelation(sigA.data(), shiftedB.data(), numSamples);
            if (corr > maxCorr) {
                maxCorr = corr;
                bestOffset = offset;
            }
        }

        REQUIRE(std::abs(bestOffset) == kKnownDelay);
        REQUIRE_THAT(maxCorr, Catch::Matchers::WithinRel(1.0f, 0.05f));
    }
}

