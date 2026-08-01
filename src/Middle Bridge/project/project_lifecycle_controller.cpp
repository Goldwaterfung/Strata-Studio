// src/Middle Bridge/project_lifecycle_controller.cpp
#include "project/project_lifecycle_controller.h"
#include <fstream>
#include <cstring>

namespace bridge {

ProjectLifecycleController::ProjectLifecycleController(
    ISessionManager* sessionManager,
    Layer3::IDSPKernel* dspKernel,
    DSP::AudioSequencerFactory* audioSequencerF,
    DSP::MidiSequencerFactory* midiSequencerF,
    DSP::LatencyFactory* latencyF,
    DSP::ChannelStripFactory* csF,
    DSP::PannerFactory* panF,
    DSP::SendFactory* sendF,
    DSP::PluginSlotFactory* slotF,
    Layer1::IFileSystem* fs,
    Layer3::IPluginManager* pluginManager,
    Layer1::IAudioDriver* audioDriver,
    Layer3::IButlerThread* butler,
    Layer2::IMutationBridge* mutationBridge,
    NodeID masterBusNode,
    NodeID masterChannelStripNode,
    NodeID masterPluginSlotNode,
    NodeID masterLatencyNode,
    DSP::SineSynthFactory* sineSynthF,
    DSP::InstrumentSlotFactory* instrumentSlotF,
    DSP::AudioInputFactory* audioInputF,
    DSP::MonitorSwitchFactory* monitorSwitchF,
    DSP::AudioTrackFactory* audioTrackF,
    DSP::InstrumentTrackFactory* instrumentTrackF
)
    : sessionManager_(sessionManager)
    , dspKernel_(dspKernel)
    , audioSequencerF_(audioSequencerF)
    , midiSequencerF_(midiSequencerF)
    , latencyF_(latencyF)
    , csF_(csF)
    , panF_(panF)
    , sendF_(sendF)
    , slotF_(slotF)
    , fs_(fs)
    , audioDriver_(audioDriver)
    , butler_(butler)
    , mutationBridge_(mutationBridge)
    , pluginManager_(pluginManager)
    , masterBusNode_(masterBusNode)
    , masterChannelStripNode_(masterChannelStripNode)
    , masterPluginSlotNode_(masterPluginSlotNode)
    , masterLatencyNode_(masterLatencyNode)
    , sineSynthF_(sineSynthF)
    , instrumentSlotF_(instrumentSlotF)
    , audioInputF_(audioInputF)
    , monitorSwitchF_(monitorSwitchF)
    , audioTrackF_(audioTrackF)
    , instrumentTrackF_(instrumentTrackF)
{
}

bool ProjectLifecycleController::createNewProject(const ProjectMetadataState& metadata) {
    std::lock_guard<std::mutex> lock(mutex_);
    isPending_.store(true);
    progress_.store(0.0f);

    suspendAudioStream();
    progress_.store(0.3f);
    missingPlugins_.clear();

    // Create a new track pipeline builder
    auto builder = std::make_unique<TrackPipelineBuilder>(
        audioSequencerF_, midiSequencerF_, latencyF_, csF_, panF_, sendF_, slotF_, fs_, butler_, mutationBridge_, masterBusNode_, sineSynthF_, instrumentSlotF_, audioInputF_, monitorSwitchF_, audioTrackF_, instrumentTrackF_
    );

    // Create active session
    auto newSession = composition::IProjectSession::create(std::move(builder), dspKernel_, mutationBridge_, pluginManager_, masterChannelStripNode_, masterPluginSlotNode_, masterLatencyNode_, latencyF_);
    if (!newSession) {
        resumeAudioStream();
        isPending_.store(false);
        return false;
    }
    progress_.store(0.6f);

    composition::ProjectMetadata meta;
    meta.projectName = metadata.projectName;
    meta.author = metadata.author;
    meta.sampleRate = metadata.sampleRate;
    meta.initialTempoBPM = metadata.initialTempoBPM;
    meta.timeSignatureNumerator = metadata.timeSignatureNumerator;
    meta.timeSignatureDenominator = metadata.timeSignatureDenominator;
    meta.targetBitDepth = metadata.targetBitDepth;
    meta.sessionDurationSeconds = metadata.sessionDurationSeconds;
    
    newSession->setMetadata(meta);

    sessionManager_->setActiveSession(std::move(newSession));
    currentProjectPath_ = "";
    progress_.store(0.9f);
    
    resumeAudioStream();
    progress_.store(1.0f);
    isPending_.store(false);
    return true;
}

bool ProjectLifecycleController::loadProject(const char* absoluteFilePath) {
    if (!absoluteFilePath || std::strlen(absoluteFilePath) == 0) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    isPending_.store(true);
    progress_.store(0.0f);

    // 1. Validate the magic header first
    std::ifstream file(absoluteFilePath, std::ios::binary);
    if (!file) {
        isPending_.store(false);
        return false;
    }
    
    char magic[14] = {0};
    file.read(magic, 13);
    if (std::strcmp(magic, "AGDAW_PROJ_V6") != 0) {
        isPending_.store(false);
        return false;
    }
    file.close();
    progress_.store(0.2f);

    // 2. Safely suspend audio thread before destroying old session
    suspendAudioStream();
    progress_.store(0.4f);

    // 3. Create a new track pipeline builder
    auto builder = std::make_unique<TrackPipelineBuilder>(
        audioSequencerF_, midiSequencerF_, latencyF_, csF_, panF_, sendF_, slotF_, fs_, butler_, mutationBridge_, masterBusNode_, sineSynthF_, instrumentSlotF_, audioInputF_, monitorSwitchF_, audioTrackF_, instrumentTrackF_
    );

    // 4. Create new blank session
    auto newSession = composition::IProjectSession::create(std::move(builder), dspKernel_, mutationBridge_, pluginManager_, masterChannelStripNode_, masterPluginSlotNode_, masterLatencyNode_, latencyF_);
    if (!newSession) {
        resumeAudioStream();
        isPending_.store(false);
        return false;
    }
    progress_.store(0.6f);

    missingPlugins_.clear();

    // 5. Load project data from file
    if (!newSession->loadFromFile(absoluteFilePath, sessionManager_->getStringRegistry(), &missingPlugins_)) {
        resumeAudioStream();
        isPending_.store(false);
        return false;
    }
    progress_.store(0.8f);

    // 6. Set as the active session
    sessionManager_->setActiveSession(std::move(newSession));
    
    currentProjectPath_ = absoluteFilePath;
    
    resumeAudioStream();
    progress_.store(1.0f);
    isPending_.store(false);
    return true;
}

bool ProjectLifecycleController::saveProject(const char* absoluteFilePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto* session = sessionManager_->getActiveSession();
    if (!session) return false;

    std::string savePath = absoluteFilePath;
    if (savePath.empty()) {
        savePath = currentProjectPath_;
    }
    
    if (savePath.empty()) {
        return false;
    }

    isPending_.store(true);
    progress_.store(0.0f);

    bool success = session->saveToFile(savePath, sessionManager_->getStringRegistry());
    progress_.store(0.8f);

    if (success) {
        currentProjectPath_ = savePath;
    }
    
    progress_.store(1.0f);
    isPending_.store(false);
    return success;
}

bool ProjectLifecycleController::exportProjectToJson(const char* absoluteFilePath) {
    if (!absoluteFilePath || std::strlen(absoluteFilePath) == 0) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    auto* session = sessionManager_->getActiveSession();
    if (!session) return false;

    isPending_.store(true);
    progress_.store(0.0f);

    bool success = session->saveToJsonFile(absoluteFilePath, sessionManager_->getStringRegistry());
    
    progress_.store(1.0f);
    isPending_.store(false);
    return success;
}

bool ProjectLifecycleController::importProjectFromJson(const char* absoluteFilePath) {
    if (!absoluteFilePath || std::strlen(absoluteFilePath) == 0) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    isPending_.store(true);
    progress_.store(0.0f);

    // Suspend audio stream
    suspendAudioStream();
    progress_.store(0.3f);

    // Create a new track pipeline builder
    auto builder = std::make_unique<TrackPipelineBuilder>(
        audioSequencerF_, midiSequencerF_, latencyF_, csF_, panF_, sendF_, slotF_, fs_, butler_, mutationBridge_, masterBusNode_, sineSynthF_, instrumentSlotF_, audioInputF_, monitorSwitchF_
    );

    // Create new blank session
    auto newSession = composition::IProjectSession::create(std::move(builder), dspKernel_, mutationBridge_, pluginManager_, masterChannelStripNode_, masterPluginSlotNode_, masterLatencyNode_, latencyF_);
    if (!newSession) {
        resumeAudioStream();
        isPending_.store(false);
        return false;
    }
    progress_.store(0.6f);

    missingPlugins_.clear();

    // Load from JSON
    if (!newSession->loadFromJsonFile(absoluteFilePath, sessionManager_->getStringRegistry(), &missingPlugins_)) {
        resumeAudioStream();
        isPending_.store(false);
        return false;
    }
    progress_.store(0.8f);

    // Set active session
    sessionManager_->setActiveSession(std::move(newSession));
    currentProjectPath_ = absoluteFilePath;
    
    resumeAudioStream();
    progress_.store(1.0f);
    isPending_.store(false);
    return true;
}

void ProjectLifecycleController::closeProject() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    suspendAudioStream();
    sessionManager_->closeActiveSession();
    currentProjectPath_ = "";
    resumeAudioStream();
}

bool ProjectLifecycleController::hasActiveProject() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessionManager_->getActiveSession() != nullptr;
}

bool ProjectLifecycleController::isProjectDirty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!sessionManager_) return false;
    auto* session = sessionManager_->getActiveSession();
    if (!session) return false;
    auto* history = session->getCommandHistory();
    return history ? history->isDirty() : false;
}

std::string ProjectLifecycleController::getCurrentProjectPath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentProjectPath_;
}

ProjectMetadataState ProjectLifecycleController::getCurrentProjectMetadata() const {
    std::lock_guard<std::mutex> lock(mutex_);
    ProjectMetadataState state{};
    
    auto* session = sessionManager_->getActiveSession();
    if (!session) return state;

    const auto& meta = session->getMetadata();
    std::strncpy(state.projectName, meta.projectName.c_str(), sizeof(state.projectName) - 1);
    state.projectName[sizeof(state.projectName) - 1] = '\0';
    
    std::strncpy(state.author, meta.author.c_str(), sizeof(state.author) - 1);
    state.author[sizeof(state.author) - 1] = '\0';
    
    state.sampleRate = meta.sampleRate;
    state.initialTempoBPM = meta.initialTempoBPM;
    state.timeSignatureNumerator = meta.timeSignatureNumerator;
    state.timeSignatureDenominator = meta.timeSignatureDenominator;
    state.targetBitDepth = meta.targetBitDepth;
    state.sessionDurationSeconds = meta.sessionDurationSeconds;

    return state;
}

MixStatisticsState ProjectLifecycleController::getMixStatisticsState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    MixStatisticsState state{};
    
    auto* session = sessionManager_->getActiveSession();
    if (!session) return state;

    const auto& stats = session->getMixStatistics();
    state.isAnalyzed = stats.isAnalyzed;
    state.integratedLoudnessLUFS = stats.integratedLoudnessLUFS;
    state.truePeakDBTP = stats.truePeakDBTP;
    state.clippingDetected = stats.clippingDetected;

    return state;
}

std::vector<composition::MissingPluginReport> ProjectLifecycleController::getMissingPluginsFromLastLoad() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return missingPlugins_;
}

bool ProjectLifecycleController::isOperationPending() const {
    return isPending_.load();
}

float ProjectLifecycleController::getOperationProgress() const {
    return progress_.load();
}

void ProjectLifecycleController::suspendAudioStream() {
    if (audioDriver_) {
        wasRunning_ = (audioDriver_->getState() == Layer1::StreamState::RUNNING);
        if (wasRunning_) {
            (void)audioDriver_->stopStream();
        }
    }
}

void ProjectLifecycleController::resumeAudioStream() {
    if (audioDriver_ && wasRunning_) {
        (void)audioDriver_->startStream();
    }
}

} // namespace bridge
