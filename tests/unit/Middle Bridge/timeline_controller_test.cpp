// tests/unit/Middle Bridge/timeline_controller_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Middle Bridge/timeline/timeline_controller.h"
#include "Core audio engine/transport/itransport.h"
#include "Core infrastructure/tempo/itempo_service.h"
#include <memory>

using namespace bridge;
using namespace Layer2;
using namespace Layer3;

namespace {

class MockTransport : public ITransport {
public:
    TransportState state = TransportState::STOPPED;
    uint64_t position = 0;
    LoopState loop;
    uint64_t seekPosition = 0;
    SeekMode seekMode = SeekMode::IMMEDIATE;
    bool seekCalled = false;
    bool updateTempoCacheCalled = false;

    MockTransport() {
        loop.mode = LoopState::LoopMode::DISABLED;
        loop.startSample = 0;
        loop.endSample = UINT64_MAX;
    }

    void play() override {
        if (recordArmed) {
            state = TransportState::RECORDING;
        } else {
            state = TransportState::PLAYING;
        }
    }

    void stop() override {
        state = TransportState::STOPPED;
    }

    bool recordArmed = false;

    bool record() override {
        state = TransportState::RECORDING;
        return true;
    }

    bool isRecordArmed() const override {
        return recordArmed;
    }

    void setRecordArmed(bool armed) override {
        recordArmed = armed;
    }

    void setState(TransportState newState) override {
        state = newState;
    }

    TransportState getState() const override {
        return state;
    }

    void seek(uint64_t pos, SeekMode mode) override {
        position = pos;
        seekPosition = pos;
        seekMode = mode;
        seekCalled = true;
    }

    uint64_t getPosition() const override {
        return position;
    }

    TransportPosition getDetailedPosition() const override {
        TransportPosition pos;
        pos.positionSample = position;
        pos.bpm = 120.0;
        pos.numerator = 4;
        pos.denominator = 4;
        pos.bar = 1;
        pos.beat = 1;
        pos.tick = 0;
        return pos;
    }

    bool advancePosition(uint32_t /*numSamples*/) override {
        return false;
    }

    void setLoopRange(uint64_t start, uint64_t end) override {
        loop.startSample = start;
        loop.endSample = end;
    }

    void setLoopEnabled(bool enabled) override {
        loop.mode = enabled ? LoopState::LoopMode::ENABLED : LoopState::LoopMode::DISABLED;
    }

    LoopState getLoopState() const override {
        return loop;
    }

    bool metronomeEnabledVal = false;
    void setMetronomeEnabled(bool enabled) override {
        metronomeEnabledVal = enabled;
    }
    bool isMetronomeEnabled() const override {
        return metronomeEnabledVal;
    }

    void setTempoService(ITempoService* /*tempoService*/) override {}
    void setStateManager(IStateManager* /*stateManager*/) override {}
    
    void updateTempoCache() override {
        updateTempoCacheCalled = true;
    }

    uint64_t createTransportSnapshot() const override { return 0; }
    bool restoreTransportSnapshot(uint64_t /*snapshotId*/) override { return true; }
    double samplesToBeats(uint64_t /*samples*/) const override { return 0.0; }
    uint64_t beatsToSamples(double /*beats*/) const override { return 0; }
};

class MockTempoService : public ITempoService {
public:
    double sampleRate = 44100.0;
    double bpm = 120.0;
    uint8_t numerator = 4;
    uint8_t denominator = 4;
    uint64_t setTempoPosition = 0;
    uint64_t setMeterPosition = 0;
    bool setTempoCalled = false;
    bool setMeterCalled = false;
    std::vector<TempoPoint> mockEvents;

    MockTempoService() {
        mockEvents.push_back(TempoPoint{0, 120.0, 4});
    }

    void setTempoAtPosition(double b, uint64_t position) override {
        bpm = b;
        setTempoPosition = position;
        setTempoCalled = true;

        auto it = std::find_if(mockEvents.begin(), mockEvents.end(), [position](const TempoPoint& e) {
            return e.positionSample == position;
        });
        if (it != mockEvents.end()) {
            it->bpm = b;
        } else {
            mockEvents.push_back(TempoPoint{position, b, 4});
            std::sort(mockEvents.begin(), mockEvents.end(), [](const TempoPoint& x, const TempoPoint& y) {
                return x.positionSample < y.positionSample;
            });
        }
    }

    void addTempoEvent(const TempoPoint& event) override {
        mockEvents.push_back(event);
        std::sort(mockEvents.begin(), mockEvents.end(), [](const TempoPoint& x, const TempoPoint& y) {
            return x.positionSample < y.positionSample;
        });
    }

    void removeTempoEventAtPosition(uint64_t position) override {
        if (position == 0) return;
        auto it = std::find_if(mockEvents.begin(), mockEvents.end(), [position](const TempoPoint& e) {
            return e.positionSample == position;
        });
        if (it != mockEvents.end()) {
            mockEvents.erase(it);
        }
    }

    void clearTempoMap() override {
        mockEvents.clear();
        mockEvents.push_back(TempoPoint{0, 120.0, 4});
    }

    void setMeterAtPosition(uint8_t num, uint8_t den, uint64_t position) override {
        numerator = num;
        denominator = den;
        setMeterPosition = position;
        setMeterCalled = true;
    }

    void addMeterEvent(const MeterPoint& /*event*/) override {}
    void clearMeterMap() override {}

    double getTempoAtPosition(uint64_t /*position*/) const override {
        return bpm;
    }

    bool getMeterAtPosition(uint64_t /*position*/, uint8_t& outNumerator, uint8_t& outDenominator) const override {
        outNumerator = numerator;
        outDenominator = denominator;
        return true;
    }

    void updateForCycle(const ProcessContext& /*context*/) override {}
    uint64_t getCyclePositionSamples() const override { return 0; }
    double getCycleBPM() const override { return bpm; }
    BBTPosition getCycleBBT() const override { return BBTPosition(); }

    uint64_t beatsToSamples(double beats) const override {
        return static_cast<uint64_t>(beats / (bpm / 60.0) * sampleRate);
    }

    double samplesToBeats(uint64_t samples) const override {
        return static_cast<double>(samples) / sampleRate * (bpm / 60.0);
    }

    BBTPosition samplesToBBT(uint64_t /*samples*/) const override {
        return BBTPosition();
    }

    uint64_t bbtToSamples(const BBTPosition& bbt) const override {
        double totalBeats = (static_cast<double>(bbt.bar) - 1.0) * numerator + (static_cast<double>(bbt.beat) - 1.0);
        return static_cast<uint64_t>(totalBeats / (bpm / 60.0) * sampleRate);
    }

    uint32_t getTempoRange(uint64_t start, uint64_t end, TempoPoint* events, uint32_t maxEvents) const override {
        if (!events || maxEvents == 0) return 0;
        uint32_t count = 0;
        for (const auto& ev : mockEvents) {
            if (ev.positionSample >= start && ev.positionSample <= end) {
                events[count++] = ev;
                if (count >= maxEvents) break;
            }
        }
        return count;
    }

    uint32_t getMeterRange(uint64_t /*start*/, uint64_t /*end*/, MeterPoint* /*events*/, uint32_t /*maxEvents*/) const override { return 0; }

    void setSampleRate(double rate) override {
        sampleRate = rate;
    }

    double getSampleRate() const override {
        return sampleRate;
    }

    void setTicksPerBeat(uint32_t /*ticksPerBeat*/) override {}
    uint32_t getTicksPerBeat() const override { return 960; }
};

} // namespace

TEST_CASE("TimelineController: Playback Controls", "[bridge][TimelineController]") {
    MockTransport mockTransport;
    MockTempoService mockTempoService;
    TimelineController controller(&mockTransport, &mockTempoService);

    SECTION("Initial States") {
        CHECK_FALSE(controller.isPlaying());
        CHECK_FALSE(controller.isRecording());
        CHECK(controller.getCurrentFrame() == 0);
        CHECK(controller.getCurrentSeconds() == Catch::Approx(0.0));
    }

    SECTION("Play/Stop/Toggle controls") {
        controller.play();
        CHECK(controller.isPlaying());
        CHECK(mockTransport.state == TransportState::PLAYING);

        controller.togglePlay();
        CHECK_FALSE(controller.isPlaying());
        CHECK(mockTransport.state == TransportState::STOPPED);

        controller.togglePlay();
        CHECK(controller.isPlaying());
        CHECK(mockTransport.state == TransportState::PLAYING);

        controller.stop();
        CHECK_FALSE(controller.isPlaying());
        CHECK(mockTransport.state == TransportState::STOPPED);
    }

    SECTION("Record arming") {
        // 1. Arming while stopped does not start playback
        controller.setRecordArmed(true);
        CHECK(controller.isRecordArmed());
        CHECK_FALSE(controller.isRecording());
        CHECK_FALSE(controller.isPlaying());
        CHECK(mockTransport.state == TransportState::STOPPED);

        // 2. Playing while armed promotes state to RECORDING
        controller.play();
        CHECK(controller.isRecording());
        CHECK(controller.isPlaying());
        CHECK(mockTransport.state == TransportState::RECORDING);

        // 3. Punch-out: Disarming while recording demotes to PLAYING
        controller.setRecordArmed(false);
        CHECK_FALSE(controller.isRecording());
        CHECK(controller.isPlaying());
        CHECK(mockTransport.state == TransportState::PLAYING);
    }
}

TEST_CASE("TimelineController: Seeking and Locating", "[bridge][TimelineController]") {
    MockTransport mockTransport;
    MockTempoService mockTempoService;
    TimelineController controller(&mockTransport, &mockTempoService);

    mockTempoService.sampleRate = 44100.0;
    mockTempoService.bpm = 120.0; // 2 beats/sec

    SECTION("Seek to Frame") {
        controller.seekToFrame(88200);
        CHECK(controller.getCurrentFrame() == 88200);
        CHECK(mockTransport.seekCalled);
        CHECK(mockTransport.seekPosition == 88200);
        CHECK(mockTransport.seekMode == ITransport::SeekMode::IMMEDIATE);
    }

    SECTION("Seek to Time Seconds") {
        controller.seekToTimeSeconds(2.5); // 2.5 * 44100 = 110250 samples
        CHECK(controller.getCurrentFrame() == 110250);
        CHECK(mockTransport.seekCalled);
        CHECK(mockTransport.seekPosition == 110250);
    }

    SECTION("Seek to Musical Grid") {
        // Bar 3, Beat 1
        // (3-1)*4 + (1-1) = 8 beats
        // 8 beats at 120 BPM = 4 seconds
        // 4 seconds at 44100 = 176400 samples
        controller.seekToMusicalGrid(3.0, 1.0);
        CHECK(controller.getCurrentFrame() == 176400);
    }
}

TEST_CASE("TimelineController: Tempo, Meter and Looping", "[bridge][TimelineController]") {
    MockTransport mockTransport;
    MockTempoService mockTempoService;
    TimelineController controller(&mockTransport, &mockTempoService);

    mockTempoService.sampleRate = 48000.0;
    mockTransport.position = 96000; // 2 seconds in

    SECTION("BPM mutation") {
        controller.setBPM(140.0);
        CHECK(mockTempoService.setTempoCalled);
        CHECK(mockTempoService.bpm == Catch::Approx(140.0));
        CHECK(mockTempoService.setTempoPosition == 0); // Always targets position 0
        CHECK(mockTransport.updateTempoCacheCalled);
        CHECK(controller.getBPM() == Catch::Approx(140.0));
    }

    SECTION("Tempo points manipulation via controller") {
        VisualTempoPoint pts[5];
        uint32_t count = controller.getTempoPoints(0, 100000, pts, 5);
        REQUIRE(count == 1);
        CHECK(pts[0].framePosition == 0);
        CHECK(pts[0].bpm == Catch::Approx(120.0));

        controller.addTempoPoint(48000, 140.0);
        count = controller.getTempoPoints(0, 100000, pts, 5);
        REQUIRE(count == 2);
        CHECK(pts[1].framePosition == 48000);
        CHECK(pts[1].bpm == Catch::Approx(140.0));

        controller.removeTempoPoint(48000);
        count = controller.getTempoPoints(0, 100000, pts, 5);
        REQUIRE(count == 1);
        CHECK(pts[0].framePosition == 0);

        controller.removeTempoPoint(0);
        count = controller.getTempoPoints(0, 100000, pts, 5);
        REQUIRE(count == 1);
        CHECK(pts[0].framePosition == 0);
    }

    SECTION("isTempoAutomated detection") {
        // Initially only 1 event at position 0 exists
        CHECK_FALSE(controller.isTempoAutomated());

        // Add a second event at a future position
        mockTempoService.addTempoEvent(ITempoService::TempoPoint{48000, 130.0, 4});
        CHECK(controller.isTempoAutomated());

        // Clear and add only 1 event at a position > 0
        mockTempoService.mockEvents.clear();
        mockTempoService.addTempoEvent(ITempoService::TempoPoint{24000, 125.0, 4});
        CHECK(controller.isTempoAutomated());
    }

    SECTION("Time Signature mutation") {
        VisualTimeSignature timeSig{3, 8};
        controller.setTimeSignature(timeSig);
        CHECK(mockTempoService.setMeterCalled);
        CHECK(mockTempoService.numerator == 3);
        CHECK(mockTempoService.denominator == 8);
        CHECK(mockTempoService.setMeterPosition == 96000);
        CHECK(mockTransport.updateTempoCacheCalled);
    }

    SECTION("Loop controls") {
        controller.setLoopRange(1000, 20000);
        controller.setLoopEnabled(true);
        CHECK(mockTransport.loop.startSample == 1000);
        CHECK(mockTransport.loop.endSample == 20000);
        CHECK(controller.isLooping());

        controller.setLoopEnabled(false);
        CHECK_FALSE(controller.isLooping());
    }
}

TEST_CASE("TimelineController: Visual conversions", "[bridge][TimelineController]") {
    MockTransport mockTransport;
    MockTempoService mockTempoService;
    TimelineController controller(&mockTransport, &mockTempoService);

    SECTION("pixelsToFrames") {
        CHECK(controller.pixelsToFrames(100.0f, 2.0f) == Catch::Approx(50.0));
        CHECK(controller.pixelsToFrames(100.0f, 0.0f) == Catch::Approx(0.0));
    }

    SECTION("framesToPixels") {
        CHECK(controller.framesToPixels(200, 1.5f) == Catch::Approx(300.0f));
        CHECK(controller.framesToPixels(200, 0.0f) == Catch::Approx(0.0f));
    }
}

#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/interfaces/imarker_manager.h"
#include "musical_composition/interfaces/itrack_pipeline_builder.h"
#include "project/isession_manager.h"
#include <cstring>

namespace {

class DummyPipelineBuilder : public composition::ITrackPipelineBuilder {
public:
    composition::TrackPipelineDescriptor buildPipeline(const composition::TrackCreateInfo&, Layer3::IDSPKernel*) override {
        return composition::TrackPipelineDescriptor{};
    }
    void destroyPipeline(const composition::TrackPipelineDescriptor&, Layer3::IDSPKernel*) override {}
};

class DummySessionManager : public bridge::ISessionManager {
public:
    explicit DummySessionManager(composition::IProjectSession* session) : session_(session) {}
    ~DummySessionManager() override = default;

    composition::IProjectSession* getActiveSession() const override { return session_; }
    void setActiveSession(std::unique_ptr<composition::IProjectSession>) override {}
    void closeActiveSession() override {}
    void registerChangeListener(bridge::ISessionChangeListener* listener) override {
        listeners_.push_back(listener);
        if (session_) {
            listener->onSessionChanged(session_);
        }
    }
    void unregisterChangeListener(bridge::ISessionChangeListener* listener) override {
        listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
    }
    void triggerSessionRefresh() override {}
    void onTempoMapChanged(Layer2::ITempoService*) override {}
    Layer2::IStringRegistry* getStringRegistry() const override { return nullptr; }

private:
    composition::IProjectSession* session_;
    std::vector<bridge::ISessionChangeListener*> listeners_;
};

} // namespace

TEST_CASE("TimelineController: Marker Calculations & Timecodes", "[bridge][TimelineController]") {
    auto builder = std::make_unique<DummyPipelineBuilder>();
    auto session = composition::IProjectSession::create(
        std::move(builder),
        nullptr, nullptr, nullptr, NodeID::invalid(), NodeID::invalid(), NodeID::invalid(), nullptr
    );
    REQUIRE(session != nullptr);

    // Set sample rate to 48000 Hz
    composition::ProjectMetadata meta = session->getMetadata();
    meta.projectName = "Controller Test";
    meta.sampleRate = 48000;
    session->setMetadata(meta);

    auto* markerMgr = session->getMarkerManager();
    // Add marker at frame 144000 (exactly 3.0 seconds)
    MarkerUUID uuid1 = markerMgr->addMarker(MarkerUUID{}, 144000, "Start Point", 0xFFFF0000);

    // Add marker at frame 256800 (5.35 seconds, 5 seconds and 10.5 frames => 11 frames)
    // 256800 / 48000.0 = 5.35
    // 0.35 * 30.0 = 10.5 => rounds to 11
    MarkerUUID uuid2 = markerMgr->addMarker(MarkerUUID{}, 256800, "Second Point", 0xFF00FF00);

    MockTransport mockTransport;
    MockTempoService mockTempoService;
    TimelineController controller(&mockTransport, &mockTempoService);
    
    auto sessionMgr = std::make_unique<DummySessionManager>(session.get());
    controller.setSessionManager(sessionMgr.get());

    std::vector<VisualMarker> vmarkers(5);
    uint32_t count = controller.getMarkersInRange(0, UINT64_MAX, vmarkers.data(), 5);
    REQUIRE(count == 2);

    CHECK(vmarkers[0].uuid == uuid1);
    CHECK(vmarkers[0].framePosition == 144000);
    CHECK(vmarkers[0].locationSeconds == 3.0);
    CHECK(vmarkers[0].markerNumber == 1);
    CHECK(std::strcmp(vmarkers[0].timecode, "00:00:03:00") == 0);

    CHECK(vmarkers[1].uuid == uuid2);
    CHECK(vmarkers[1].framePosition == 256800);
    CHECK(vmarkers[1].locationSeconds == Catch::Approx(5.35));
    CHECK(vmarkers[1].markerNumber == 2);
    CHECK(std::strcmp(vmarkers[1].timecode, "00:00:05:10") == 0);

    // Update marker through controller
    controller.updateMarker(uuid1, 48000, "Updated Start", 0xFFFFFFFF);
    count = controller.getMarkersInRange(0, UINT64_MAX, vmarkers.data(), 5);
    REQUIRE(count == 2);
    // Since uuid1 is at frame 48000 now, and uuid2 is at 256800, their order is still [uuid1, uuid2].
    CHECK(vmarkers[0].uuid == uuid1);
    CHECK(vmarkers[0].framePosition == 48000);
    CHECK(std::strcmp(vmarkers[0].label, "Updated Start") == 0);
    CHECK(vmarkers[0].locationSeconds == 1.0);
    CHECK(std::strcmp(vmarkers[0].timecode, "00:00:01:00") == 0);

    // Remove marker through controller
    controller.removeMarker(uuid1);
    count = controller.getMarkersInRange(0, UINT64_MAX, vmarkers.data(), 5);
    REQUIRE(count == 1);
    CHECK(vmarkers[0].uuid == uuid2);
    CHECK(vmarkers[0].markerNumber == 1); // dynamic reindexing
}
