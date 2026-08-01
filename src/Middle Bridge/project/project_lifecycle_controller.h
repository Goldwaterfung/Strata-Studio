// src/Middle Bridge/project_lifecycle_controller.h
#pragma once

#include "project/iproject_lifecycle_controller.h"
#include "project/isession_manager.h"
#include "tracks/track_pipeline_builder.h"
#include "Core audio engine/scheduler/idsp_kernel.h"
#include "Hardware/OS abstraction/audio/iaudio_driver.h"
#include "Hardware/OS abstraction/filesystem/ifile_system.h"
#include <mutex>
#include <atomic>
#include <string>

namespace Layer3 {
    class IDSPKernel;
    class IButlerThread;
    class IPluginManager;
}

namespace Layer1 {
    class IFileSystem;
    class IAudioDriver;
}

namespace bridge {

class ProjectLifecycleController : public IProjectLifecycleController {
public:
    ProjectLifecycleController(
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
        Layer3::IPluginManager* pluginManager = nullptr,
        Layer1::IAudioDriver* audioDriver = nullptr,
        Layer3::IButlerThread* butler = nullptr,
        Layer2::IMutationBridge* mutationBridge = nullptr,
        NodeID masterBusNode = NodeID::invalid(),
        NodeID masterChannelStripNode = NodeID::invalid(),
        NodeID masterPluginSlotNode = NodeID::invalid(),
        NodeID masterLatencyNode = NodeID::invalid(),
        DSP::SineSynthFactory* sineSynthF = nullptr,
        DSP::InstrumentSlotFactory* instrumentSlotF = nullptr,
        DSP::AudioInputFactory* audioInputF = nullptr,
        DSP::MonitorSwitchFactory* monitorSwitchF = nullptr,
        DSP::AudioTrackFactory* audioTrackF = nullptr,
        DSP::InstrumentTrackFactory* instrumentTrackF = nullptr
    );
    
    ~ProjectLifecycleController() override = default;

    // Prevent copying
    ProjectLifecycleController(const ProjectLifecycleController&) = delete;
    ProjectLifecycleController& operator=(const ProjectLifecycleController&) = delete;

    // --- IProjectLifecycleController Interface ---
    bool createNewProject(const ProjectMetadataState& metadata) override;
    bool loadProject(const char* absoluteFilePath) override;
    bool saveProject(const char* absoluteFilePath = "") override;
    bool exportProjectToJson(const char* absoluteFilePath) override;
    bool importProjectFromJson(const char* absoluteFilePath) override;
    void closeProject() override;

    bool hasActiveProject() const override;
    bool isProjectDirty() const override;
    std::string getCurrentProjectPath() const override;
    ProjectMetadataState getCurrentProjectMetadata() const override;
    MixStatisticsState getMixStatisticsState() const override;
    std::vector<composition::MissingPluginReport> getMissingPluginsFromLastLoad() const override;

    bool isOperationPending() const override;
    float getOperationProgress() const override;

private:
    // Helper to stop/restart the audio stream safely around transitions
    void suspendAudioStream();
    void resumeAudioStream();

    ISessionManager* sessionManager_;
    Layer3::IDSPKernel* dspKernel_;
    DSP::AudioSequencerFactory* audioSequencerF_;
    DSP::MidiSequencerFactory* midiSequencerF_;
    DSP::LatencyFactory* latencyF_;
    DSP::ChannelStripFactory* csF_;
    DSP::PannerFactory* panF_;
    DSP::SendFactory* sendF_;
    DSP::PluginSlotFactory* slotF_;
    Layer1::IFileSystem* fs_;
    Layer1::IAudioDriver* audioDriver_;
    Layer3::IButlerThread* butler_;
    Layer2::IMutationBridge* mutationBridge_;
    Layer3::IPluginManager* pluginManager_;
    NodeID masterBusNode_;
    NodeID masterChannelStripNode_;
    NodeID masterPluginSlotNode_;
    NodeID masterLatencyNode_;
    DSP::SineSynthFactory* sineSynthF_;
    DSP::InstrumentSlotFactory* instrumentSlotF_;
    DSP::AudioInputFactory* audioInputF_;
    DSP::MonitorSwitchFactory* monitorSwitchF_;
    DSP::AudioTrackFactory* audioTrackF_;
    DSP::InstrumentTrackFactory* instrumentTrackF_;

    std::string currentProjectPath_;
    bool wasRunning_ = false;
    std::vector<composition::MissingPluginReport> missingPlugins_;

    // Async operation progress simulation
    std::atomic<bool> isPending_{false};
    std::atomic<float> progress_{0.0f};

    mutable std::mutex mutex_;
};

} // namespace bridge
