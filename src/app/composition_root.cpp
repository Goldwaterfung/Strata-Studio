#include "composition_root.h"
#include "Hardware/OS abstraction/audio/iaudio_driver.h"
#include "Hardware/OS abstraction/filesystem/ifile_system.h"
#include "Hardware/OS abstraction/midi/imidi_driver.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "Core infrastructure/bridges/imutation_bridge.h"
#include "Core infrastructure/bridges/itelemetry_bridge.h"
#include "Core infrastructure/bridges/ievent_queue.h"
#include "Core infrastructure/clock/iclock_service.h"
#include "Core infrastructure/tempo/itempo_service.h"
#include "Core audio engine/scheduler/idsp_kernel.h"
#include "Core audio engine/transport/itransport.h"
#include "Core audio engine/engine/iaudio_engine.h"
#include "Core audio engine/plugin/iplugin_manager.h"
#include "Core audio engine/sidechain/isidechain_manager.h"
#include "common/dsp/node_types.h"
#include "DSP nodes/sampler/sampler_node.h"
#include "DSP nodes/sequencer/audio_sequencer_node.h"
#include "DSP nodes/sequencer/midi_sequencer_node.h"
#include "DSP nodes/latency/latency_node.h"
#include "DSP nodes/sine_synth/sine_synth_node.h"
#include "DSP nodes/channelstrip/channel_strip_node.h"
#include "DSP nodes/panner/panner_node.h"
#include "DSP nodes/sends/send_node.h"
#include "DSP nodes/buses/bus_node.h"
#include "common/dsp/automation_fsm.h"
#include "DSP nodes/analysis/analysis_node.h"
#include "DSP nodes/plugins/plugin_slot_node.h"
#include "DSP nodes/plugins/instrument_slot_node.h"
#include "DSP nodes/audio_input/audio_input_node.h"
#include "DSP nodes/monitor_switch/monitor_switch_node.h"
#include "DSP nodes/tracks/audio_track_node.h"
#include "DSP nodes/tracks/instrument_track_node.h"
#include "Middle Bridge/telemetry/metering_provider.h"
#include "Middle Bridge/engine/hardware_settings_facade.h"
#include "Middle Bridge/project/session_manager.h"
#include "Middle Bridge/project/project_lifecycle_controller.h"
#include "Middle Bridge/tracks/track_controller.h"
#include "Middle Bridge/timeline/arrangement_controller.h"
#include "Middle Bridge/automation/automation_controller.h"
#include "Core audio engine/automation/iautomation_processor.h"
#include "musical_composition/automation/iautomation_capture_engine.h"
#include "Middle Bridge/browser/browser_controller.h"
#include "Middle Bridge/timeline/timeline_controller.h"
#include "Middle Bridge/engine/input_mode_controller.h"
#include "Middle Bridge/project/workspace_controller.h"
#include "Middle Bridge/midi/midi_editor_controller.h"
#include "Middle Bridge/telemetry/waveform_cache_provider.h"
#include "Middle Bridge/telemetry/pattern_data_provider.h"
#include "Middle Bridge/timeline/arrangement_manager_controller.h"
#include "Middle Bridge/engine/render_controller.h"
#include "Media management/export/iexport_service.h"
#include "Media management/registry/imedia_registry.h"
#include "Media management/codecs/icodec_factory.h"
#include "Media management/analysis/iaudio_analysis_engine.h"
#include "Media management/waveforms/iwaveform_renderer.h"
#include "Media management/browser/iproject_browser.h"
#include "Media management/library/isample_library_browser.h"
#include "Middle Bridge/automation/iautomation_recording_gateway.h"
#include "musical_composition/recording/take_recording_intake.h"
#include "recording/recording_controller.h"
#include "Media management/recording/disk_writer_service_impl.h"
#include "Media management/intake/imedia_intake_pipeline.h"
#include <iostream>
#include <cstring>
#include <cstdlib>

namespace app {

CompositionRoot& CompositionRoot::instance() {
    static CompositionRoot instance;
    return instance;
}

CompositionRoot::~CompositionRoot() {
    std::cout << "CompositionRoot: Tearing down layers..." << std::endl;
    if (audioDriver_) {
        std::cout << "CompositionRoot: Stopping audio stream to prevent callback crashes during teardown..." << std::endl;
        (void)audioDriver_->stopStream();
        audioDriver_->closeStream();
    }
}

bool CompositionRoot::wireLayer1ToLayer2() {
    std::cout << "CompositionRoot: Wiring Layer 1 -> Layer 2..." << std::endl;

    // 1. Instantiate Layer 1 components (Hardware/OS Abstraction)
    fileSystem_ = Layer1::IFileSystem::create();
    if (!fileSystem_) {
        std::cerr << "CompositionRoot: Failed to create FileSystem" << std::endl;
        return false;
    }

    audioDriver_ = Layer1::IAudioDriver::create(Layer1::AudioAPI::SHARED);
    if (!audioDriver_) {
        std::cerr << "CompositionRoot: Failed to create AudioDriver" << std::endl;
        return false;
    }

    midiDriver_ = Layer1::IMIDIDriver::create(Layer1::AudioAPI::SHARED);
    if (!midiDriver_) {
        std::cerr << "CompositionRoot: Failed to create MIDIDriver" << std::endl;
        return false;
    }

    // 2. Instantiate Layer 2 components (Core Infrastructure)
    stringRegistry_ = Layer2::IStringRegistry::create();
    if (!stringRegistry_) {
        std::cerr << "CompositionRoot: Failed to create StringRegistry" << std::endl;
        return false;
    }

    mutationBridge_ = Layer2::IMutationBridge::create(1024);
    if (!mutationBridge_) {
        std::cerr << "CompositionRoot: Failed to create MutationBridge" << std::endl;
        return false;
    }

    telemetryBridge_ = Layer2::ITelemetryBridge::create(1024);
    if (!telemetryBridge_) {
        std::cerr << "CompositionRoot: Failed to create TelemetryBridge" << std::endl;
        return false;
    }

    eventQueue_ = Layer2::IEventQueue::create(Layer2::IEventQueue::Config::defaultConfig());
    if (!eventQueue_) {
        std::cerr << "CompositionRoot: Failed to create EventQueue" << std::endl;
        return false;
    }

    clockService_ = Layer2::IClockService::create();
    if (!clockService_) {
        std::cerr << "CompositionRoot: Failed to create ClockService" << std::endl;
        return false;
    }

    tempoService_ = Layer2::ITempoService::create();
    if (!tempoService_) {
        std::cerr << "CompositionRoot: Failed to create TempoService" << std::endl;
        return false;
    }

    // Configure core infrastructure default parameters
    tempoService_->setSampleRate(44100.0);
    clockService_->setSampleRate(44100.0);
    tempoService_->setTempoAtPosition(120.0, 0);

    return true;
}

bool CompositionRoot::wireLayer2ToLayer3() {
    std::cout << "CompositionRoot: Wiring Layer 2 -> Layer 3..." << std::endl;

    // 1. Instantiate Layer 3 components (Core Audio Engine)
    scheduler_ = Layer3::IDSPKernel::create(256);
    if (!scheduler_) {
        std::cerr << "CompositionRoot: Failed to create DSPKernel scheduler" << std::endl;
        return false;
    }

    transport_ = Layer3::ITransport::create(44100);
    if (!transport_) {
        std::cerr << "CompositionRoot: Failed to create Transport" << std::endl;
        return false;
    }

    audioEngine_ = Layer3::IAudioEngine::create();
    if (!audioEngine_) {
        std::cerr << "CompositionRoot: Failed to create AudioEngine" << std::endl;
        return false;
    }

    pluginManager_ = Layer3::IPluginManager::create();
    if (!pluginManager_) {
        std::cerr << "CompositionRoot: Failed to create PluginManager" << std::endl;
        return false;
    }

    automationProcessor_ = Layer3::IAutomationProcessor::create();
    if (!automationProcessor_) {
        std::cerr << "CompositionRoot: Failed to create AutomationProcessor" << std::endl;
        return false;
    }
    audioEngine_->setAutomationProcessor(automationProcessor_.get());

    sidechainManager_ = Layer3::ISidechainManager::create();
    if (!sidechainManager_) {
        std::cerr << "CompositionRoot: Failed to create SidechainManager" << std::endl;
        return false;
    }

    automationMonitor_ = std::make_unique<DSP::AutomationMonitor>();
    automationMonitor_->setSampleRate(44100.0);

    // 2. Wire Layer 2 to Layer 3
    transport_->setTempoService(tempoService_.get());
    
    scheduler_->attachMutationBridge(mutationBridge_.get());
    scheduler_->attachTelemetryBridge(telemetryBridge_.get());
    scheduler_->attachEventQueue(eventQueue_.get());
    scheduler_->attachSidechainManager(sidechainManager_.get());

    audioEngine_->setScheduler(scheduler_.get());
    audioEngine_->setTransport(transport_.get());
    audioEngine_->setClockService(clockService_.get());
    audioEngine_->setMutationBridge(mutationBridge_.get());
    audioEngine_->setEventQueue(eventQueue_.get());
    audioEngine_->setTelemetryBridge(telemetryBridge_.get());
    audioEngine_->setTempoService(tempoService_.get());
    audioEngine_->setMIDIDriver(midiDriver_.get());
    audioEngine_->setFileSystem(fileSystem_.get());

    return true;
}

bool CompositionRoot::wireLayer3ToLayer4() {
    std::cout << "CompositionRoot: Wiring Layer 3 -> Layer 4..." << std::endl;

    // 1. Instantiate Layer 4 components (DSP Processing Node Factories)
    samplerFactory_ = std::make_unique<DSP::SamplerFactory>();
    audioSequencerFactory_ = std::make_unique<DSP::AudioSequencerFactory>();
    midiSequencerFactory_ = std::make_unique<DSP::MidiSequencerFactory>();
    latencyFactory_ = std::make_unique<DSP::LatencyFactory>();
    channelStripFactory_ = std::make_unique<DSP::ChannelStripFactory>();
    pannerFactory_ = std::make_unique<DSP::PannerFactory>();
    sendFactory_ = std::make_unique<DSP::SendFactory>();
    pluginSlotFactory_ = std::make_unique<DSP::PluginSlotFactory>();
    busFactory_ = std::make_unique<DSP::BusFactory>();
    analysisFactory_ = std::make_unique<DSP::AnalysisFactory>();
    sineSynthFactory_ = std::make_unique<DSP::SineSynthFactory>();
    instrumentSlotFactory_ = std::make_unique<DSP::InstrumentSlotFactory>();
    audioInputFactory_ = std::make_unique<DSP::AudioInputFactory>();
    monitorSwitchFactory_ = std::make_unique<DSP::MonitorSwitchFactory>();
    audioTrackFactory_ = std::make_unique<DSP::AudioTrackFactory>();
    instrumentTrackFactory_ = std::make_unique<DSP::InstrumentTrackFactory>();

    // Set static dependencies for AudioSequencerFactory
    if (audioEngine_) {
        DSP::AudioSequencerFactory::setButlerThread(audioEngine_->getButlerThread());
    }
    DSP::AudioSequencerFactory::setFileSystem(fileSystem_.get());

    // 2. Register node factories and processing functions in Layer 3's scheduler
    scheduler_->registerFactory(DSP::NODE_TYPE_SAMPLER, samplerFactory_.get());
    scheduler_->registerFactory(DSP::NODE_TYPE_AUDIO_SEQUENCER, audioSequencerFactory_.get());
    scheduler_->registerFactory(DSP::NODE_TYPE_MIDI_SEQUENCER, midiSequencerFactory_.get());
    scheduler_->registerFactory(DSP::NODE_TYPE_LATENCY, latencyFactory_.get());
    scheduler_->registerFactory(DSP::NODE_TYPE_CHANNEL_STRIP, channelStripFactory_.get());
    scheduler_->registerFactory(DSP::NODE_TYPE_PANNER, pannerFactory_.get());
    scheduler_->registerFactory(DSP::NODE_TYPE_SEND, sendFactory_.get());
    scheduler_->registerFactory(DSP::NODE_TYPE_PLUGIN_SLOT, pluginSlotFactory_.get());
    scheduler_->registerFactory(DSP::NODE_TYPE_BUS, busFactory_.get());
    scheduler_->registerFactory(DSP::NODE_TYPE_ANALYSIS, analysisFactory_.get());
    scheduler_->registerFactory(DSP::NODE_TYPE_SINE_SYNTH, sineSynthFactory_.get());
    scheduler_->registerFactory(DSP::NODE_TYPE_INSTRUMENT_SLOT, instrumentSlotFactory_.get());
    scheduler_->registerFactory(DSP::NODE_TYPE_AUDIO_INPUT, audioInputFactory_.get());
    scheduler_->registerFactory(DSP::NODE_TYPE_MONITOR_SWITCH, monitorSwitchFactory_.get());
    scheduler_->registerFactory(DSP::NODE_TYPE_AUDIO_TRACK, audioTrackFactory_.get());
    scheduler_->registerFactory(DSP::NODE_TYPE_INSTRUMENT_TRACK, instrumentTrackFactory_.get());

    scheduler_->registerProcessor(DSP::NODE_TYPE_SAMPLER, DSP::processSampler);
    scheduler_->registerProcessor(DSP::NODE_TYPE_AUDIO_SEQUENCER, DSP::processAudioSequencer);
    scheduler_->registerProcessor(DSP::NODE_TYPE_MIDI_SEQUENCER, DSP::processMidiSequencer);
    scheduler_->registerProcessor(DSP::NODE_TYPE_LATENCY, DSP::processLatency);
    scheduler_->registerProcessor(DSP::NODE_TYPE_CHANNEL_STRIP, DSP::processChannelStrip);
    scheduler_->registerProcessor(DSP::NODE_TYPE_PANNER, DSP::processPanner);
    scheduler_->registerProcessor(DSP::NODE_TYPE_SEND, DSP::processSend);
    scheduler_->registerProcessor(DSP::NODE_TYPE_PLUGIN_SLOT, DSP::processPluginSlot);
    scheduler_->registerProcessor(DSP::NODE_TYPE_BUS, DSP::processBus);
    scheduler_->registerProcessor(DSP::NODE_TYPE_ANALYSIS, DSP::processAnalysis);
    scheduler_->registerProcessor(DSP::NODE_TYPE_SINE_SYNTH, DSP::processSineSynth);
    scheduler_->registerProcessor(DSP::NODE_TYPE_INSTRUMENT_SLOT, DSP::processInstrumentSlot);
    scheduler_->registerProcessor(DSP::NODE_TYPE_AUDIO_INPUT, DSP::processAudioInput);
    scheduler_->registerProcessor(DSP::NODE_TYPE_MONITOR_SWITCH, DSP::processMonitorSwitch);
    scheduler_->registerProcessor(DSP::NODE_TYPE_AUDIO_TRACK, DSP::processAudioTrack);
    scheduler_->registerProcessor(DSP::NODE_TYPE_INSTRUMENT_TRACK, DSP::processInstrumentTrack);

    DSP::setChannelStripAutomationMonitor(automationMonitor_.get());
    DSP::setPannerAutomationMonitor(automationMonitor_.get());
    DSP::setBusAutomationMonitor(automationMonitor_.get());
    DSP::setSendAutomationMonitor(automationMonitor_.get());

    return true;
}

bool CompositionRoot::wireLayer4ToLayer5() {
    std::cout << "CompositionRoot: Wiring Layer 4 -> Layer 5..." << std::endl;
    // Layer 5 (Musical Composition / Session) will be created inside the ProjectLifecycleController
    // using the factories registered here.
    return true;
}

bool CompositionRoot::wireLayer5ToLayer6() {
    std::cout << "CompositionRoot: Wiring Layer 5 -> Layer 6..." << std::endl;

    mediaRegistry_ = MediaManagement::IMediaRegistry::create();
    codecFactory_ = MediaManagement::ICodecFactory::create();
    audioAnalysisEngine_ = MediaManagement::IAudioAnalysisEngine::create(
        mediaRegistry_.get(),
        stringRegistry_.get(),
        codecFactory_.get()
    );
    waveformRenderer_ = MediaManagement::IWaveformRenderer::create(
        mediaRegistry_.get(),
        stringRegistry_.get(),
        codecFactory_.get()
    );
    projectBrowser_ = MediaManagement::IProjectBrowser::create(
        mediaRegistry_.get(),
        stringRegistry_.get(),
        codecFactory_.get(),
        audioAnalysisEngine_.get(),
        waveformRenderer_.get()
    );
    sampleLibraryBrowser_ = MediaManagement::ISampleLibraryBrowser::create(
        "./samples.db",
        mediaRegistry_.get(),
        stringRegistry_.get(),
        nullptr, // previewBuilder can be null
        codecFactory_.get(),
        telemetryBridge_.get()
    );

    mediaIntakePipeline_ = MediaManagement::IMediaIntakePipeline::create(
        mediaRegistry_.get(),
        stringRegistry_.get(),
        codecFactory_.get(),
        audioAnalysisEngine_.get(),
        waveformRenderer_.get()
    );

    diskWriterService_ = std::make_shared<MediaManagement::DiskWriterServiceImpl>();
    diskWriterService_->start();

    return true;
}

bool CompositionRoot::wireLayer6ToLayer7() {
    std::cout << "CompositionRoot: Wiring Layer 6 -> Layer 7 (Middle Bridge & Controllers)..." << std::endl;

    // 1. Instantiate SessionManager
    auto sm = std::make_unique<bridge::SessionManager>();
    if (audioEngine_) {
        sm->setAudioEngine(audioEngine_.get());
    }
    sm->setStringRegistry(stringRegistry_.get());
    sm->setIntakePipeline(mediaIntakePipeline_.get());
    sm->setMutationBridge(mutationBridge_.get());

#ifdef __APPLE__
    std::string tempDir = std::string(getenv("HOME")) + "/DAW/Untitled Project/Recordings/temp/";
#elif defined(_WIN32)
    std::string tempDir = std::string(getenv("USERPROFILE")) + "\\Documents\\DAW\\Recordings\\temp\\";
#else
    std::string tempDir = "/tmp/DAW/Untitled Project/Recordings/temp/";
#endif

    sessionManager_ = std::move(sm);

    takeRecordingIntake_ = std::make_unique<composition::TakeRecordingIntake>(
        stringRegistry_.get()
    );

    recordingController_ = std::make_unique<bridge::RecordingController>();
    recordingController_->setSessionManager(sessionManager_.get());
    recordingController_->setMutationBridge(mutationBridge_.get());
    recordingController_->setTakeRecordingIntake(takeRecordingIntake_.get());
    recordingController_->setDiskWriterService(diskWriterService_);
    recordingController_->setTempRecordingDirectory(tempDir);
    recordingController_->setCodecFactory(codecFactory_.get());
    recordingController_->setAudioEngine(audioEngine_.get());
    recordingController_->setIntakePipeline(mediaIntakePipeline_.get());

    recordingGateway_ = bridge::IAutomationRecordingGateway::create(
        sessionManager_.get(),
        stringRegistry_.get(),
        automationProcessor_.get()
    );

    automationCaptureEngine_ = composition::IAutomationCaptureEngine::create(
        stringRegistry_.get(),
        automationProcessor_.get(),
        automationMonitor_.get(),
        recordingGateway_.get()
    );
    if (!automationCaptureEngine_) {
        std::cerr << "CompositionRoot: Failed to create AutomationCaptureEngine" << std::endl;
        return false;
    }

    if (audioEngine_) {
        auto* playheadRenderer = dynamic_cast<Layer3::IAudioEngine::IMIDIPlayheadRenderer*>(sessionManager_.get());
        if (playheadRenderer) {
            audioEngine_->setMIDIPlayheadRenderer(playheadRenderer);
        }
        auto* provider = dynamic_cast<const IMidiClipDataProvider*>(sessionManager_.get());
        audioEngine_->setMidiClipDataProvider(provider);
    }

    // 1b. Create master bus channel strip DSP node for direct mixer control
    if (channelStripFactory_) {
        masterChannelStripNode_ = channelStripFactory_->createNode();
    }
    if (busFactory_) {
        masterSumBusNode_ = busFactory_->createNode();
    }
    if (pluginSlotFactory_) {
        masterPluginSlotNode_ = pluginSlotFactory_->createNode();
        if (auto* s = pluginSlotFactory_->getRegistry().get(masterPluginSlotNode_)) {
            s->reset();
        }
    }
    if (latencyFactory_) {
        masterLatencyNode_ = latencyFactory_->createNode();
        latencyFactory_->setLatency(masterLatencyNode_, 0);
    }
    if (analysisFactory_) {
        masterAnalysisNode_ = analysisFactory_->createNode();
    }

    // 2. Instantiate Metering & Telemetry Facade Provider
    meteringProvider_ = std::make_unique<bridge::MeteringProvider>(
        telemetryBridge_.get(), 
        sessionManager_.get()
    );

    // Register the master analysis node so its telemetry frames are routed
    // to masterMeterState_ (not dropped due to unknown sourceId).
    if (masterAnalysisNode_.isValid()) {
        meteringProvider_->registerMasterAnalysisNode(masterAnalysisNode_);
    }

    // 3. Instantiate Waveform Cache & Pattern Data Providers
    waveformCacheProvider_ = std::make_unique<bridge::WaveformCacheProvider>(
        waveformRenderer_.get()
    );
    patternDataProvider_ = std::make_unique<bridge::PatternDataProvider>(
        sessionManager_.get()
    );

    // 3b. Instantiate Hardware Settings Facade
    hardwareSettingsFacade_ = std::make_unique<bridge::HardwareSettingsFacade>(
        audioDriver_.get(),
        midiDriver_.get(),
        audioEngine_.get(),
        sessionManager_.get()
    );

    // 4. Instantiate Middle Bridge Controllers with concrete dependencies
    trackController_ = std::make_unique<bridge::TrackController>(
        sessionManager_.get(),
        mutationBridge_.get(),
        stringRegistry_.get(),
        meteringProvider_.get(),
        pluginManager_.get(),
        masterChannelStripNode_,
        masterSumBusNode_,
        masterPluginSlotNode_,
        masterLatencyNode_,
        latencyFactory_.get(),
        transport_.get(),
        recordingGateway_.get(),
        hardwareSettingsFacade_.get()
    );
    trackController_->setRecordingController(recordingController_.get());

    arrangementController_ = std::make_unique<bridge::ArrangementController>(
        sessionManager_.get(),
        stringRegistry_.get(),
        mediaRegistry_.get(),
        waveformRenderer_.get(),
        fileSystem_.get()
    );
    arrangementController_->setRecordingController(recordingController_.get());

    automationController_ = std::make_unique<bridge::AutomationController>(
        sessionManager_.get(),
        stringRegistry_.get(),
        transport_.get(),
        automationCaptureEngine_.get(),
        automationProcessor_.get(),
        automationMonitor_.get(),
        recordingGateway_.get()
    );

    trackController_->setAutomationLaneRequestCallback(
        [this](TrackID trackId, NodeID routingNodeId, uint32_t subNodeId, uint32_t parameterIndex) {
            if (automationController_) {
                automationController_->createAutomationLane(trackId, routingNodeId, subNodeId, parameterIndex);
            }
        }
    );

    projectLifecycleController_ = std::make_unique<bridge::ProjectLifecycleController>(
        sessionManager_.get(),
        scheduler_.get(),
        audioSequencerFactory_.get(),
        midiSequencerFactory_.get(),
        latencyFactory_.get(),
        channelStripFactory_.get(),
        pannerFactory_.get(),
        sendFactory_.get(),
        pluginSlotFactory_.get(),
        fileSystem_.get(),
        pluginManager_.get(),
        audioDriver_.get(),
        audioEngine_->getButlerThread(),
        mutationBridge_.get(),
        masterSumBusNode_,
        masterChannelStripNode_,
        masterPluginSlotNode_,
        masterLatencyNode_,
        sineSynthFactory_.get(),
        instrumentSlotFactory_.get(),
        audioInputFactory_.get(),
        monitorSwitchFactory_.get(),
        audioTrackFactory_.get(),
        instrumentTrackFactory_.get()
    );

    browserController_ = std::make_unique<bridge::BrowserController>(
        sessionManager_.get(),
        stringRegistry_.get(),
        projectBrowser_.get(),
        sampleLibraryBrowser_.get(),
        mediaRegistry_.get(),
        pluginManager_.get()
    );

    timelineController_ = std::make_unique<bridge::TimelineController>(
        transport_.get(),
        tempoService_.get()
    );
    timelineController_->setSessionManager(sessionManager_.get());
    timelineController_->setStringRegistry(stringRegistry_.get());
    timelineController_->setAudioDriver(audioDriver_.get());
    timelineController_->setAutomationController(automationController_.get());
    timelineController_->setRecordingController(recordingController_.get());

    inputModeController_ = std::make_unique<bridge::InputModeController>();

    workspaceController_ = std::make_unique<bridge::WorkspaceController>();

    midiEditorController_ = std::make_unique<bridge::MidiEditorController>(
        sessionManager_.get(),
        stringRegistry_.get(),
        tempoService_.get(),
        eventQueue_.get(),
        clockService_.get()
    );

    // 4b. Instantiate Export Service and new controllers
    exportService_ = MediaManagement::IExportService::create(
        mediaRegistry_.get(),
        stringRegistry_.get(),
        scheduler_.get(),
        dynamic_cast<const IMidiClipDataProvider*>(sessionManager_.get())
    );

    arrangementManagerController_ = std::make_unique<bridge::ArrangementManagerController>(
        sessionManager_.get()
    );

    renderController_ = std::make_unique<bridge::RenderController>(
        exportService_.get(),
        stringRegistry_.get(),
        sessionManager_.get()
    );
    renderController_->setAudioEngine(audioEngine_.get());

    // 4. Register Controllers as ISessionChangeListener Observers
    sessionManager_->registerChangeListener(trackController_.get());
    sessionManager_->registerChangeListener(arrangementController_.get());
    sessionManager_->registerChangeListener(automationController_.get());
    sessionManager_->registerChangeListener(browserController_.get());
    sessionManager_->registerChangeListener(midiEditorController_.get());
    sessionManager_->registerChangeListener(arrangementManagerController_.get());

    // 5. Bootstrap default active ProjectSession immediately so getActiveSession() is never null
    bridge::ProjectMetadataState defaultMeta{};
    std::strcpy(defaultMeta.projectName, "Untitled Project");
    std::strcpy(defaultMeta.author, "DAW User");
    defaultMeta.sampleRate = 44100;
    defaultMeta.initialTempoBPM = 120.0f;
    defaultMeta.timeSignatureNumerator = 4;
    defaultMeta.timeSignatureDenominator = 4;

    if (!projectLifecycleController_->createNewProject(defaultMeta)) {
        std::cerr << "CompositionRoot: Warning - Failed to bootstrap initial project session" << std::endl;
    } else {
        std::cout << "CompositionRoot: Initial project session successfully bootstrapped" << std::endl;
    }

    return true;
}

bridge::IMeteringProvider* CompositionRoot::getMeteringProvider() const { return meteringProvider_.get(); }
bridge::ITrackController* CompositionRoot::getTrackController() const { return trackController_.get(); }
bridge::IArrangementController* CompositionRoot::getArrangementController() const { return arrangementController_.get(); }
bridge::IAutomationController* CompositionRoot::getAutomationController() const { return automationController_.get(); }
bridge::IBrowserController* CompositionRoot::getBrowserController() const { return browserController_.get(); }
bridge::ITimelineController* CompositionRoot::getTimelineController() const { return timelineController_.get(); }
bridge::IInputModeController* CompositionRoot::getInputModeController() const { return inputModeController_.get(); }
bridge::IWorkspaceController* CompositionRoot::getWorkspaceController() const { return workspaceController_.get(); }
bridge::IMidiEditorController* CompositionRoot::getMidiEditorController() const { return midiEditorController_.get(); }
bridge::IArrangementManagerController* CompositionRoot::getArrangementManagerController() const { return arrangementManagerController_.get(); }
bridge::IRenderController* CompositionRoot::getRenderController() const { return renderController_.get(); }

bool CompositionRoot::connectAudioToBridges() {
    std::cout << "CompositionRoot: Connecting audio to bridges..." << std::endl;

    if (!audioEngine_ || !audioDriver_) {
        std::cerr << "CompositionRoot: Cannot connect audio, engine or driver is null" << std::endl;
        return false;
    }

    // 1. Prepare Audio Engine for hardware callbacks
    audioEngine_->prepare(44100.0, 512);

    // 2. Open Stream and connect it to the AudioEngine client callback
    Layer1::IAudioDriver::StreamConfig config{};
    config.inputDeviceIndex = 0;
    config.outputDeviceIndex = 0;
    config.numInputChannels = 2;
    config.numOutputChannels = 2;
    config.sampleRate = 44100;
    config.bufferSize = 512;
    config.client = audioEngine_.get();

    Layer1::OpenResult result = audioDriver_->openStream(config);
    if (!result.success) {
        std::cerr << "CompositionRoot: Warning - Failed to open audio hardware stream: " 
                  << (result.errorMessage[0] != '\0' ? result.errorMessage : "unknown error") << std::endl;
        // Don't fail the entire startup sequence - let GUI boot up
    } else {
        std::cout << "CompositionRoot: Audio hardware stream opened successfully. Starting playback thread..." << std::endl;
        if (!audioDriver_->startStream()) {
            std::cerr << "CompositionRoot: Failed to start audio hardware stream callback" << std::endl;
        }
    }

    return true;
}

bool CompositionRoot::connectTransportToScheduler() {
    std::cout << "CompositionRoot: Connecting transport to scheduler..." << std::endl;
    return true;
}

bool CompositionRoot::connectDSPToComposition() {
    std::cout << "CompositionRoot: Connecting DSP to composition..." << std::endl;

    // Register the master channel strip node in the DSP kernel's processing graph
    // so that it appears in the topological execution order.
    if (mutationBridge_) {
        SystemMutation m{};
        m.priority = 0;
        
        if (masterChannelStripNode_.isValid()) {
            m.type = Layer2::MutationType::NODE_ADD;
            m.node.type = DSP::NODE_TYPE_CHANNEL_STRIP;
            m.node.id = masterChannelStripNode_;
            mutationBridge_->pushMutation(m);
        }
        
        if (masterSumBusNode_.isValid()) {
            m.type = Layer2::MutationType::NODE_ADD;
            m.node.type = DSP::NODE_TYPE_BUS;
            m.node.id = masterSumBusNode_;
            mutationBridge_->pushMutation(m);
        }

        if (masterPluginSlotNode_.isValid()) {
            m.type = Layer2::MutationType::NODE_ADD;
            m.node.type = DSP::NODE_TYPE_PLUGIN_SLOT;
            m.node.id = masterPluginSlotNode_;
            mutationBridge_->pushMutation(m);
        }

        if (masterLatencyNode_.isValid()) {
            m.type = Layer2::MutationType::NODE_ADD;
            m.node.type = DSP::NODE_TYPE_LATENCY;
            m.node.id = masterLatencyNode_;
            mutationBridge_->pushMutation(m);
        }
        
        if (masterAnalysisNode_.isValid()) {
            m.type = Layer2::MutationType::NODE_ADD;
            m.node.type = DSP::NODE_TYPE_ANALYSIS;
            m.node.id = masterAnalysisNode_;
            mutationBridge_->pushMutation(m);
        }
        
        // Connect masterSumBusNode_ -> masterPluginSlotNode_ -> masterLatencyNode_ -> masterChannelStripNode_ -> masterAnalysisNode_
        for (uint32_t ch = 0; ch < 2; ++ch) {
            if (masterSumBusNode_.isValid() && masterPluginSlotNode_.isValid()) {
                SystemMutation mConn{};
                mConn.type = Layer2::MutationType::NODE_CONNECT;
                mConn.connection.sourceNodeIndex = (DSP::NODE_TYPE_BUS << 16) | (masterSumBusNode_.id & 0xFFFF);
                mConn.connection.sourcePort = ch;
                mConn.connection.destNodeIndex = (DSP::NODE_TYPE_PLUGIN_SLOT << 16) | (masterPluginSlotNode_.id & 0xFFFF);
                mConn.connection.destPort = ch;
                mConn.connection.gain = 1.0f;
                mutationBridge_->pushMutation(mConn);
            }

            if (masterPluginSlotNode_.isValid() && masterLatencyNode_.isValid()) {
                SystemMutation mConn{};
                mConn.type = Layer2::MutationType::NODE_CONNECT;
                mConn.connection.sourceNodeIndex = (DSP::NODE_TYPE_PLUGIN_SLOT << 16) | (masterPluginSlotNode_.id & 0xFFFF);
                mConn.connection.sourcePort = ch;
                mConn.connection.destNodeIndex = (DSP::NODE_TYPE_LATENCY << 16) | (masterLatencyNode_.id & 0xFFFF);
                mConn.connection.destPort = ch;
                mConn.connection.gain = 1.0f;
                mutationBridge_->pushMutation(mConn);
            }

            if (masterLatencyNode_.isValid() && masterChannelStripNode_.isValid()) {
                SystemMutation mConn{};
                mConn.type = Layer2::MutationType::NODE_CONNECT;
                mConn.connection.sourceNodeIndex = (DSP::NODE_TYPE_LATENCY << 16) | (masterLatencyNode_.id & 0xFFFF);
                mConn.connection.sourcePort = ch;
                mConn.connection.destNodeIndex = (DSP::NODE_TYPE_CHANNEL_STRIP << 16) | (masterChannelStripNode_.id & 0xFFFF);
                mConn.connection.destPort = ch;
                mConn.connection.gain = 1.0f;
                mutationBridge_->pushMutation(mConn);
            }
            
            if (masterChannelStripNode_.isValid() && masterAnalysisNode_.isValid()) {
                SystemMutation mConn{};
                mConn.type = Layer2::MutationType::NODE_CONNECT;
                mConn.connection.sourceNodeIndex = (DSP::NODE_TYPE_CHANNEL_STRIP << 16) | (masterChannelStripNode_.id & 0xFFFF);
                mConn.connection.sourcePort = ch;
                mConn.connection.destNodeIndex = (DSP::NODE_TYPE_ANALYSIS << 16) | (masterAnalysisNode_.id & 0xFFFF);
                mConn.connection.destPort = ch;
                mConn.connection.gain = 1.0f;
                mutationBridge_->pushMutation(mConn);
            }
        }
        
        std::cout << "CompositionRoot: Master bus topology registered and connected in DSP graph" << std::endl;
    }

    return true;
}

bool CompositionRoot::connectMediaToPresentation() {
    std::cout << "CompositionRoot: Connecting media to presentation..." << std::endl;
    return true;
}

std::vector<std::string> CompositionRoot::getDefaultPluginScanDirectories() {
    std::vector<std::string> paths;
#if defined(__APPLE__)
    paths.push_back("/Library/Audio/Plug-Ins/VST3");
    paths.push_back("/Library/Audio/Plug-Ins/Components");
    paths.push_back("/Library/Audio/Plug-Ins/CLAP");
    if (const char* home = std::getenv("HOME")) {
        paths.push_back(std::string(home) + "/Library/Audio/Plug-Ins/VST3");
        paths.push_back(std::string(home) + "/Library/Audio/Plug-Ins/Components");
        paths.push_back(std::string(home) + "/Library/Audio/Plug-Ins/CLAP");
    }
#elif defined(_WIN32)
    paths.push_back("C:\\Program Files\\Common Files\\VST3");
    paths.push_back("C:\\Program Files\\Common Files\\CLAP");
#endif
    return paths;
}

} // namespace app
