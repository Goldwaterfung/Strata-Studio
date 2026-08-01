// tests/unit/Middle Bridge/session_management_test.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Middle Bridge/project/session_manager.h"
#include "Middle Bridge/project/project_lifecycle_controller.h"
#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/interfaces/iregion_metadata_manager.h"
#include "Hardware/OS abstraction/audio/iaudio_driver.h"
#include "Hardware/OS abstraction/filesystem/ifile_system.h"
#include "Core audio engine/scheduler/idsp_kernel.h"
#include <memory>
#include <vector>
#include <cstring>
#include <fstream>

namespace Layer2 { class IEventQueue; }

namespace {

// Mock IAudioDriver
class MockAudioDriverForSession : public Layer1::IAudioDriver {
public:
    Layer1::StreamState state = Layer1::StreamState::IDLE;
    uint32_t stopCalls = 0;
    uint32_t startCalls = 0;

    Layer1::OpenResult openStream(const StreamConfig&) override {
        state = Layer1::StreamState::OPEN;
        return {.success = true};
    }
    bool startStream() override {
        startCalls++;
        state = Layer1::StreamState::RUNNING;
        return true;
    }
    bool stopStream() override {
        stopCalls++;
        state = Layer1::StreamState::OPEN;
        return true;
    }
    void closeStream() override {
        state = Layer1::StreamState::IDLE;
    }
    Layer1::StreamState getState() const override {
        return state;
    }
    Layer1::IAudioDriver::StreamConfig getStreamConfig() const override {
        return {};
    }
    uint32_t getDeviceCount() const override { return 0; }
    Layer1::DeviceInfo getDeviceInfo(uint32_t) const override { return {}; }
};

// Mock IDSPKernel
class DummyDSPKernel : public Layer3::IDSPKernel {
public:
    void attachMutationBridge(Layer2::IMutationBridge*) override {}
    void attachTelemetryBridge(Layer2::ITelemetryBridge*) override {}
    void attachEventQueue(Layer2::IEventQueue*) override {}
    void attachSidechainManager(Layer3::ISidechainManager*) override {}
    void registerProcessor(uint32_t, ::DSPProcessFunc) override {}
    void unregisterProcessor(uint32_t) override {}
    void registerFactory(uint32_t, DSP::IDSPNodeFactory*) override {}
    uint32_t getNodeLatency(NodeID) const override { return 0; }
    void setNodeLatency(NodeID, uint32_t) override {}
    uint32_t getTotalLatency() const override { return 0; }
    void applyPDC() override {}
    void setPDCCrossfade(uint32_t) override {}
    void process(float *const *, float *const *, uint32_t, uint32_t, const ProcessContext*) override {}
    void setLiveMIDITargets(const NodeID*, uint32_t) override {}
    void publishTimelineSnapshot(const TimelineSnapshot&) override {}
    const TimelineSnapshot* getActiveTimelineSnapshot() const override { return nullptr; }
    uint32_t getTopologyVersion() const override { return 0; }
    uint32_t getNodeCount() const override { return 0; }
    uint32_t getConnectionCount() const override { return 0; }
    bool hasCycles() const override { return false; }
};

// Mock IFileSystem
class MockFileSystem : public Layer1::IFileSystem {
public:
    bool exists(const char*) override { return true; }
    Layer1::FileHandle openFile(const char*, bool) override { return 1; }
    void closeFile(Layer1::FileHandle) override {}
    uint64_t getFileSize(Layer1::FileHandle) override { return 0; }

    [[nodiscard]] Layer1::OperationHandle readFileAsync(Layer1::FileHandle,
                                         uint64_t,
                                         uint64_t,
                                         IAsyncCallback*) override { return 0; }

    [[nodiscard]] Layer1::OperationHandle writeFileAsync(Layer1::FileHandle,
                                           uint64_t,
                                           uint8_t*,
                                           uint64_t,
                                           IAsyncCallback*) override { return 0; }

    [[nodiscard]] bool cancelOperation(Layer1::OperationHandle) override { return true; }

    [[nodiscard]] bool setPriority(Layer1::FileHandle, Layer1::IOPriority) override { return true; }

    uint64_t readFileSync(Layer1::FileHandle,
                          uint64_t,
                          uint8_t*,
                          uint64_t bytesToRead) override { return bytesToRead; }

    uint64_t writeFileSync(Layer1::FileHandle,
                           uint64_t,
                           const uint8_t*,
                           uint64_t bytesToWrite) override { return bytesToWrite; }

    bool iterateDirectory(const char*, 
                          const std::function<void(const Layer1::FileInfo&)>&) override { return false; }

    bool getPathInfo(const char*, Layer1::FileInfo&) override { return false; }
};

class MockTrackManagerForSession : public composition::ITrackManager {
public:
    std::vector<TrackID> ids;

    TrackID createTrack(const composition::TrackCreateInfo&) override { return {0, 0}; }
    void deleteTrack(TrackID) override {}
    void renameTrack(TrackID, uint32_t) override {}
    void setTrackComments(TrackID, uint32_t) override {}
    void setTrackOutputRouting(TrackID, TrackID) override {}
    void moveTrack(TrackID, uint32_t, TrackID) override {}
    void setTrackColor(TrackID, uint32_t) override {}
    void setTrackRecordArmed(TrackID, bool) override {}
    void setTrackInputMonitoring(TrackID, bool) override {}
    void setTrackType(TrackID, composition::TrackType) override {}
    void setTrackTakesExpanded(TrackID, bool) override {}
    void setTrackLocked(TrackID, bool) override {}
    bool isTrackLocked(TrackID) const override { return false; }

    composition::IPlaylist* getPlaylist(TrackID) override { return nullptr; }
    composition::IMIDISequencer* getMIDISequencer(TrackID) override { return nullptr; }
    composition::IAutomationLaneManager* getAutomationManager(TrackID) override { return nullptr; }

    std::atomic<uint64_t>* getRecordingStartSample(TrackID) override { return nullptr; }
    composition::TrackPipelineDescriptor getPipelineDescriptor(TrackID) const override { return {}; }
    NodeID getTrackOutputNode(TrackID) const override { return NodeID::invalid(); }

    std::vector<TrackID> getAllTrackIDs() const override { return ids; }
    bool getTrackInfo(TrackID, composition::TrackCreateInfo& outInfo) const override {
        outInfo.type = composition::TrackType::AUDIO;
        outInfo.nameId = 0;
        outInfo.colorARGB = 0xFFFFFFFF;
        outInfo.audioChannelCount = 2;
        return true;
    }
    uint32_t getTrackIndexPosition(TrackID) const override { return 0; }
    TrackID getTrackParentFolderId(TrackID) const override { return {0, 0}; }

    void renderMIDIPlayback(
        uint64_t,
        uint32_t,
        bool,
        uint64_t,
        uint64_t,
        Layer2::IEventQueue*,
        bool
    ) override {}
    void compileTimelineSnapshot() override {}
    void setProjectSampleRate(uint32_t) override {}
    void recalculateTimeCaches(Layer2::ITempoService*) override {}

    // Mixer Operations
    void setTrackFaderGain(TrackID, float) override {}
    void setTrackPan(TrackID, float) override {}
    void setTrackMute(TrackID, bool) override {}
    void setTrackSolo(TrackID, bool) override {}

    // Mixer Queries
    float getTrackFaderGain(TrackID) const override { return 1.0f; }
    float getTrackPan(TrackID) const override { return 0.5f; }
    bool getTrackMute(TrackID) const override { return false; }
    bool getTrackSolo(TrackID) const override { return false; }

    // Routing Operations
    void setTrackSendGain(TrackID, bool, uint32_t, float) override {}
    void setTrackSendPan(TrackID, bool, uint32_t, float) override {}
    void setTrackSendEnabled(TrackID, bool, uint32_t, bool) override {}
    void setTrackSendDestination(TrackID, bool, uint32_t, NodeID) override {}
    void setTrackAudioInputChannel(TrackID, uint32_t, uint32_t) override {}

    // Routing Queries
    float getTrackSendGain(TrackID, bool, uint32_t) const override { return 0.0f; }
    float getTrackSendPan(TrackID, bool, uint32_t) const override { return 0.5f; }
    bool getTrackSendEnabled(TrackID, bool, uint32_t) const override { return false; }
    NodeID getTrackSendDestination(TrackID, bool, uint32_t) const override { return NodeID::invalid(); }
    std::string getTrackSendDestinationName(TrackID, bool, uint32_t) const override { return ""; }

    // Plugin Operations
    void insertTrackPlugin(TrackID, uint32_t, uint32_t) override {}
    void removeTrackPlugin(TrackID, uint32_t) override {}
    void setTrackPluginBypassed(TrackID, uint32_t, bool) override {}
    void insertTrackInstrument(TrackID, uint32_t) override {}
    void removeTrackInstrument(TrackID) override {}
    void setTrackInstrumentBypassed(TrackID, bool) override {}
    void completeTrackInstrumentInsertion(TrackID, void*, const struct PluginDescriptor&) override {}
    void completeTrackPluginInsertion(TrackID, uint32_t, void*, const struct PluginDescriptor&) override {}

    // Sidechain Operations
    bool setTrackSidechainRouting(TrackID, uint32_t, TrackID, float) override { return true; }
    void clearTrackSidechainRouting(TrackID, uint32_t) override {}
    bool getTrackSidechainRouting(TrackID, uint32_t, TrackID&, float&) const override { return false; }
    bool detectFeedbackCycle(TrackID, TrackID) const override { return false; }

    // Callback Registration
    void registerMixerRoutingCallback(composition::MixerRoutingCallback) override {}
};

class MockRegionMetadataManager : public composition::IRegionMetadataManager {
public:
    void getRegionMetadata(RegionID, composition::RegionMetadata&) const override {}
    bool hasRegionMetadata(RegionID) const override { return false; }
    void setRegionMetadata(RegionID, const composition::RegionMetadata&, bool) override {}
    void removeRegionMetadata(RegionID, bool) override {}
    void clear() override {}
};

// Concrete mock implementation of IProjectSession for pure session-manager tests
class PureMockProjectSession : public composition::IProjectSession {
public:
    composition::ProjectMetadata metadata;
    std::string savedPath;
    std::string loadedPath;
    composition::ITrackManager* trackManager = nullptr;
    MockRegionMetadataManager metaManager;

    composition::ITrackManager* getTrackManager() override { return trackManager; }
    composition::IArrangementManager* getArrangementManager() override { return nullptr; }
    composition::ICommandHistory* getCommandHistory() override { return nullptr; }
    composition::IArrangerTrack* getArrangerTrack() override { return nullptr; }
    composition::IChordTrack* getChordTrack() override { return nullptr; }
    composition::IAudioRegionSourceManager* getRegionSourceManager() override { return nullptr; }
    composition::IClipboard* getClipboard() override { return nullptr; }
    composition::ICompingEngine* getCompingEngine() override { return nullptr; }
    composition::IMarkerManager* getMarkerManager() override { return nullptr; }
    composition::IKeySignatureMap* getKeySignatureMap() override { return nullptr; }
    composition::IRegionMetadataManager* getRegionMetadataManager() override { return &metaManager; }

    const composition::ProjectMetadata& getMetadata() const override { return metadata; }
    void setMetadata(const composition::ProjectMetadata& meta) override { metadata = meta; }

    composition::MixStatistics getMixStatistics() const override { return mixStats; }
    void setMixStatistics(const composition::MixStatistics& stats) override { mixStats = stats; }

    bool saveToFile(const std::string& absolutePath, Layer2::IStringRegistry* = nullptr) override {
        savedPath = absolutePath;
        return true;
    }
    bool loadFromFile(const std::string& absolutePath, Layer2::IStringRegistry* = nullptr, std::vector<composition::MissingPluginReport>* = nullptr) override {
        loadedPath = absolutePath;
        return true;
    }
    bool saveToJsonFile(const std::string& absolutePath, Layer2::IStringRegistry* = nullptr) override {
        savedPath = absolutePath;
        return true;
    }
    bool loadFromJsonFile(const std::string& absolutePath, Layer2::IStringRegistry* = nullptr, std::vector<composition::MissingPluginReport>* = nullptr) override {
        loadedPath = absolutePath;
        return true;
    }

    composition::MixStatistics mixStats;
};

// Concrete observer mock
class MockSessionChangeListener : public bridge::ISessionChangeListener {
public:
    int changingCount = 0;
    int changedCount = 0;
    composition::IProjectSession* lastNewSession = nullptr;

    void onSessionChanging() override {
        changingCount++;
    }

    void onSessionChanged(composition::IProjectSession* newSession) override {
        changedCount++;
        lastNewSession = newSession;
    }
};

} // namespace

TEST_CASE("SessionManager: Basic Lifecycle & Observer Notifications", "[MiddleBridge][Session]") {
    bridge::SessionManager sessionManager;
    MockSessionChangeListener listener;

    sessionManager.registerChangeListener(&listener);

    // Initial state
    REQUIRE(sessionManager.getActiveSession() == nullptr);

    // Swap active session
    auto session = std::make_unique<PureMockProjectSession>();
    session->metadata.projectName = "Test Session";
    composition::IProjectSession* rawSessionPtr = session.get();

    sessionManager.setActiveSession(std::move(session));

    // Verify getter and observer notifications
    CHECK(sessionManager.getActiveSession() == rawSessionPtr);
    CHECK(listener.changingCount == 1);
    CHECK(listener.changedCount == 1);
    CHECK(listener.lastNewSession == rawSessionPtr);

    // Close session
    sessionManager.closeActiveSession();

    CHECK(sessionManager.getActiveSession() == nullptr);
    CHECK(listener.changingCount == 2);
    CHECK(listener.changedCount == 2);
    CHECK(listener.lastNewSession == nullptr);

    // Unregister observer
    sessionManager.unregisterChangeListener(&listener);

    auto newSession = std::make_unique<PureMockProjectSession>();
    sessionManager.setActiveSession(std::move(newSession));

    // Observer count should not increment after unregistering
    CHECK(listener.changingCount == 2);
    CHECK(listener.changedCount == 2);
}

TEST_CASE("ProjectLifecycleController: Operations & State Tracking", "[MiddleBridge][Lifecycle]") {
    bridge::SessionManager sessionManager;
    DummyDSPKernel kernel;
    MockFileSystem fs;
    MockAudioDriverForSession driver;
    
    driver.state = Layer1::StreamState::RUNNING; // Start as running

    bridge::ProjectLifecycleController controller(
        &sessionManager,
        &kernel,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        &fs,
        nullptr, // pluginManager
        &driver
    );

    // Initial checks
    REQUIRE_FALSE(controller.hasActiveProject());
    REQUIRE(controller.getCurrentProjectPath().empty());

    // 1. Create New Project
    bridge::ProjectMetadataState metadata{};
    std::strcpy(metadata.projectName, "Epic Beat");
    std::strcpy(metadata.author, "DAW Producer");
    metadata.sampleRate = 48000;
    metadata.initialTempoBPM = 120.0f;
    metadata.timeSignatureNumerator = 4;
    metadata.timeSignatureDenominator = 4;

    bool success = controller.createNewProject(metadata);
    REQUIRE(success);
    
    // Verify driver suspension around creation
    CHECK(driver.stopCalls == 1);
    CHECK(driver.startCalls == 1);

    // Verify state queries
    CHECK(controller.hasActiveProject());
    CHECK(controller.getCurrentProjectPath().empty());
    
    // Verify metadata conversion to state POD
    auto activeMeta = controller.getCurrentProjectMetadata();
    CHECK(std::strcmp(activeMeta.projectName, "Epic Beat") == 0);
    CHECK(std::strcmp(activeMeta.author, "DAW Producer") == 0);
    CHECK(activeMeta.sampleRate == 48000);
    CHECK(activeMeta.initialTempoBPM == Catch::Approx(120.0f));

    // 2. Save project (Should fail without target path if never saved)
    success = controller.saveProject();
    CHECK_FALSE(success);

    // Save with target path
    const char* tempFilePath = "temp_unit_test_project.agdaw";
    success = controller.saveProject(tempFilePath);
    REQUIRE(success);
    CHECK(controller.getCurrentProjectPath() == tempFilePath);

    // 3. Load Project
    // First close the active project
    controller.closeProject();
    CHECK_FALSE(controller.hasActiveProject());

    // Try loading (which should now read our saved file and validate the header)
    success = controller.loadProject(tempFilePath);
    REQUIRE(success);
    CHECK(controller.hasActiveProject());
    CHECK(controller.getCurrentProjectPath() == tempFilePath);

    // Verify loaded metadata
    activeMeta = controller.getCurrentProjectMetadata();
    CHECK(std::strcmp(activeMeta.projectName, "Epic Beat") == 0);

    // Clean up temporary file
    std::remove(tempFilePath);
}

TEST_CASE("ProjectLifecycleController: Format Header Validation", "[MiddleBridge][Lifecycle]") {
    bridge::SessionManager sessionManager;
    DummyDSPKernel kernel;
    MockFileSystem fs;
    MockAudioDriverForSession driver;

    bridge::ProjectLifecycleController controller(
        &sessionManager, &kernel,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        &fs,
        nullptr, // pluginManager
        &driver
    );

    // Write a dummy file with incorrect header
    const char* invalidFilePath = "invalid_project.agdaw";
    {
        std::ofstream file(invalidFilePath, std::ios::binary);
        file.write("NOT_A_VALID_HEADER", 18);
    }

    // Try loading the invalid file
    bool success = controller.loadProject(invalidFilePath);
    CHECK_FALSE(success);
    CHECK_FALSE(controller.hasActiveProject());

    std::remove(invalidFilePath);
}

#include "Middle Bridge/tracks/track_controller.h"

TEST_CASE("SessionManager & TrackController: Dangling Pointer Mitigation", "[MiddleBridge][Session]") {
    bridge::SessionManager sessionManager;
    bridge::TrackController trackController(&sessionManager, nullptr, nullptr, nullptr);

    // Initial state: no active project session
    CHECK(trackController.getTrackCount() == 0);
    CHECK(trackController.getAllTracks().empty());

    // 1. Load First Project Session
    auto session1 = std::make_unique<PureMockProjectSession>();
    MockTrackManagerForSession trackManager1;
    trackManager1.ids = {{1, 1}, {2, 1}};
    session1->trackManager = &trackManager1;

    sessionManager.setActiveSession(std::move(session1));
    CHECK(trackController.getTrackCount() == 2);
    auto tracks1 = trackController.getAllTracks();
    REQUIRE(tracks1.size() == 2);
    CHECK(tracks1[0].trackId.id == 1);
    CHECK(tracks1[1].trackId.id == 2);

    // 2. Load Second Project Session (causing first to be destroyed/swapped)
    auto session2 = std::make_unique<PureMockProjectSession>();
    MockTrackManagerForSession trackManager2;
    trackManager2.ids = {{3, 1}};
    session2->trackManager = &trackManager2;

    sessionManager.setActiveSession(std::move(session2));

    // Verify track count reflects new session without crash or referring to first session's trackManager
    CHECK(trackController.getTrackCount() == 1);
    auto tracks2 = trackController.getAllTracks();
    REQUIRE(tracks2.size() == 1);
    CHECK(tracks2[0].trackId.id == 3);

    // 3. Close active project
    sessionManager.closeActiveSession();
    CHECK(trackController.getTrackCount() == 0);
    CHECK(trackController.getAllTracks().empty());
}
