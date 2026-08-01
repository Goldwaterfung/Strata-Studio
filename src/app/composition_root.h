#pragma once

#include <memory>
#include "Middle Bridge/engine/ihardware_settings_facade.h"
#include "common/system_primitives.h"

// Forward declarations from Layers 1 to 4 and Middle Bridge
namespace Layer1 {
class IAudioDriver;
class IFileSystem;
class IMIDIDriver;
}

namespace Layer2 {
class IStringRegistry;
class IMutationBridge;
class ITelemetryBridge;
class IEventQueue;
class IClockService;
class ITempoService;
}

namespace Layer3 {
class IDSPKernel;
class ITransport;
class IAudioEngine;
class IPluginManager;
class IAutomationProcessor;
class ISidechainManager;
}

namespace bridge {
class BootController;
}

namespace DSP {
class SamplerFactory;
class AudioSequencerFactory;
class MidiSequencerFactory;
class LatencyFactory;
class ChannelStripFactory;
class PannerFactory;
class SendFactory;
class BusFactory;
class AnalysisFactory;
class PluginSlotFactory;
class SineSynthFactory;
class InstrumentSlotFactory;
class AudioInputFactory;
class MonitorSwitchFactory;
class AudioTrackFactory;
class InstrumentTrackFactory;
class AutomationMonitor;
}

namespace bridge {
class ISessionManager;
class IProjectLifecycleController;
class IMeteringProvider;
class ITrackController;
class IArrangementController;
class IAutomationController;
class IBrowserController;
class ITimelineController;
class IInputModeController;
class IWorkspaceController;
class IMidiEditorController;
class IArrangementManagerController;
class IRenderController;
class TrackController;
class ArrangementController;
class AutomationController;
class BrowserController;
class TimelineController;
class InputModeController;
class WorkspaceController;
class MidiEditorController;
class MeteringProvider;
class IWaveformCacheProvider;
class IPatternDataProvider;
class ArrangementManagerController;
class RenderController;
class IAutomationRecordingGateway;
class RecordingController;
}

namespace composition {
class IAutomationCaptureEngine;
class ITakeRecordingIntake;
}

namespace MediaManagement {
class IMediaRegistry;
class ICodecFactory;
class IAudioAnalysisEngine;
class IWaveformRenderer;
class IProjectBrowser;
class ISampleLibraryBrowser;
class IExportService;
class IDiskWriterService;
class IMediaIntakePipeline;
}

namespace app {

/**
 * @brief Composition Root - Dependency Injection and Layer Wiring
 *
 * This singleton is the only place in the system that knows about the
 * concrete implementations of all layers. It performs dependency injection
 * and cross-layer wiring, ensuring that layers remain decoupled through
 * interfaces.
 *
 * The Composition Root is instantiated after all layers have been
 * initialized and before the main application loop begins.
 */
class CompositionRoot {
public:
    static CompositionRoot& instance();

    // Prevent copying
    CompositionRoot(const CompositionRoot&) = delete;
    CompositionRoot& operator=(const CompositionRoot&) = delete;

    // --- Middle Bridge Controllers Getters ---
    bridge::ISessionManager* getSessionManager() const { return sessionManager_.get(); }
    bridge::IProjectLifecycleController* getProjectLifecycleController() const { return projectLifecycleController_.get(); }
    bridge::IMeteringProvider* getMeteringProvider() const;
    bridge::ITrackController* getTrackController() const;
    bridge::IArrangementController* getArrangementController() const;
    bridge::IAutomationController* getAutomationController() const;
    bridge::IBrowserController* getBrowserController() const;
    bridge::ITimelineController* getTimelineController() const;
    bridge::IInputModeController* getInputModeController() const;
    bridge::IWorkspaceController* getWorkspaceController() const;
    bridge::IMidiEditorController* getMidiEditorController() const;
    bridge::IArrangementManagerController* getArrangementManagerController() const;
    bridge::IRenderController* getRenderController() const;
    bridge::IWaveformCacheProvider* getWaveformCacheProvider() const { return waveformCacheProvider_.get(); }
    bridge::IPatternDataProvider* getPatternDataProvider() const { return patternDataProvider_.get(); }
    bridge::IHardwareSettingsFacade* getHardwareSettingsFacade() const { return hardwareSettingsFacade_.get(); }

    /**
     * @brief Get the master bus channel strip DSP node ID.
     *        Used by the mixer master strip for direct parameter control.
     */
    NodeID getMasterChannelStripNode() const { return masterChannelStripNode_; }

    /**
     * @brief Get centralized system-default plugin search directories.
     */
    static std::vector<std::string> getDefaultPluginScanDirectories();

private:
    friend class bridge::BootController;

    CompositionRoot() = default;
    ~CompositionRoot();

    // Layer wiring helpers
    bool wireLayer1ToLayer2();
    bool wireLayer2ToLayer3();
    bool wireLayer3ToLayer4();
    bool wireLayer4ToLayer5();
    bool wireLayer5ToLayer6();
    bool wireLayer6ToLayer7();

    // Cross-layer connection helpers
    bool connectAudioToBridges();
    bool connectTransportToScheduler();
    bool connectDSPToComposition();
    bool connectMediaToPresentation();

    // --- Concrete Layer Instances (Phase 1) ---
    // Layer 1
    std::unique_ptr<Layer1::IAudioDriver> audioDriver_;
    std::unique_ptr<Layer1::IFileSystem> fileSystem_;
    std::unique_ptr<Layer1::IMIDIDriver> midiDriver_;

    // Layer 2
    std::unique_ptr<Layer2::IStringRegistry> stringRegistry_;
    std::unique_ptr<Layer2::IMutationBridge> mutationBridge_;
    std::unique_ptr<Layer2::ITelemetryBridge> telemetryBridge_;
    std::unique_ptr<Layer2::IEventQueue> eventQueue_;
    std::unique_ptr<Layer2::IClockService> clockService_;
    std::unique_ptr<Layer2::ITempoService> tempoService_;

    // Layer 3
    std::unique_ptr<Layer3::IDSPKernel> scheduler_;
    std::unique_ptr<Layer3::ITransport> transport_;
    std::unique_ptr<Layer3::IAudioEngine> audioEngine_;
    std::unique_ptr<Layer3::IPluginManager> pluginManager_;
    std::unique_ptr<Layer3::IAutomationProcessor> automationProcessor_;
    std::unique_ptr<Layer3::ISidechainManager> sidechainManager_;
    std::unique_ptr<DSP::AutomationMonitor> automationMonitor_;

    // Layer 4 Factories
    std::unique_ptr<DSP::SamplerFactory> samplerFactory_;
    std::unique_ptr<DSP::AudioSequencerFactory> audioSequencerFactory_;
    std::unique_ptr<DSP::MidiSequencerFactory> midiSequencerFactory_;
    std::unique_ptr<DSP::LatencyFactory> latencyFactory_;
    std::unique_ptr<DSP::ChannelStripFactory> channelStripFactory_;
    std::unique_ptr<DSP::PannerFactory> pannerFactory_;
    std::unique_ptr<DSP::SendFactory> sendFactory_;
    std::unique_ptr<DSP::PluginSlotFactory> pluginSlotFactory_;
    std::unique_ptr<DSP::BusFactory> busFactory_;
    std::unique_ptr<DSP::AnalysisFactory> analysisFactory_;
    std::unique_ptr<DSP::SineSynthFactory> sineSynthFactory_;
    std::unique_ptr<DSP::InstrumentSlotFactory> instrumentSlotFactory_;
    std::unique_ptr<DSP::AudioInputFactory> audioInputFactory_;
    std::unique_ptr<DSP::MonitorSwitchFactory> monitorSwitchFactory_;
    std::unique_ptr<DSP::AudioTrackFactory> audioTrackFactory_;
    std::unique_ptr<DSP::InstrumentTrackFactory> instrumentTrackFactory_;

    // Layer 6 Services (Concrete)
    std::unique_ptr<MediaManagement::IMediaRegistry> mediaRegistry_;
    std::unique_ptr<MediaManagement::ICodecFactory> codecFactory_;
    std::unique_ptr<MediaManagement::IAudioAnalysisEngine> audioAnalysisEngine_;
    std::unique_ptr<MediaManagement::IWaveformRenderer> waveformRenderer_;
    std::unique_ptr<MediaManagement::IProjectBrowser> projectBrowser_;
    std::unique_ptr<MediaManagement::ISampleLibraryBrowser> sampleLibraryBrowser_;
    std::unique_ptr<MediaManagement::IMediaIntakePipeline> mediaIntakePipeline_;
    std::shared_ptr<MediaManagement::IDiskWriterService> diskWriterService_;

    // Layer 7 and Middle Bridge Services
    std::unique_ptr<bridge::ISessionManager> sessionManager_;
    std::unique_ptr<composition::ITakeRecordingIntake> takeRecordingIntake_;
    std::unique_ptr<bridge::RecordingController> recordingController_;
    std::unique_ptr<bridge::IAutomationRecordingGateway> recordingGateway_;
    std::unique_ptr<composition::IAutomationCaptureEngine> automationCaptureEngine_;
    
    std::unique_ptr<bridge::MeteringProvider> meteringProvider_;
    std::unique_ptr<bridge::IWaveformCacheProvider> waveformCacheProvider_;
    std::unique_ptr<bridge::IPatternDataProvider> patternDataProvider_;
    std::unique_ptr<bridge::IHardwareSettingsFacade> hardwareSettingsFacade_;

    std::unique_ptr<bridge::TrackController> trackController_;
    std::unique_ptr<bridge::ArrangementController> arrangementController_;
    std::unique_ptr<bridge::AutomationController> automationController_;
    std::unique_ptr<bridge::IProjectLifecycleController> projectLifecycleController_;
    std::unique_ptr<bridge::BrowserController> browserController_;
    std::unique_ptr<bridge::TimelineController> timelineController_;
    std::unique_ptr<bridge::InputModeController> inputModeController_;
    std::unique_ptr<bridge::WorkspaceController> workspaceController_;
    std::unique_ptr<bridge::MidiEditorController> midiEditorController_;
    std::unique_ptr<MediaManagement::IExportService> exportService_;
    std::unique_ptr<bridge::ArrangementManagerController> arrangementManagerController_;
    std::unique_ptr<bridge::RenderController> renderController_;

    // Master bus DSP node
    NodeID masterChannelStripNode_;
    NodeID masterSumBusNode_;
    NodeID masterAnalysisNode_;
    NodeID masterPluginSlotNode_;
    NodeID masterLatencyNode_;
};

} // namespace app

