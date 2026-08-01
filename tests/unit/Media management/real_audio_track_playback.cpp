// tests/unit/Media management/real_audio_track_playback.cpp
#include "Core audio engine/scheduler/idsp_kernel.h"
#include "Core audio engine/streaming/ibutler_thread.h"
#include "Core audio engine/transport/itransport.h"
#include "Core infrastructure/clock/iclock_service.h"
#include "Core infrastructure/bridges/imutation_bridge.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "DSP nodes/analysis/analysis_node.h"
#include "DSP nodes/buses/bus_node.h"
#include "DSP nodes/channelstrip/channel_strip_node.h"
#include "DSP nodes/latency/latency_node.h"
#include "DSP nodes/panner/panner_node.h"
#include "DSP nodes/sequencer/audio_sequencer_node.h"
#include "DSP nodes/sequencer/midi_sequencer_node.h"
#include "DSP nodes/sends/send_node.h"

#include "Hardware/OS abstraction/filesystem/ifile_system.h"
#include "Hardware/OS abstraction/audio/iaudio_driver.h"
#include "Core audio engine/engine/iaudio_engine.h"
#include "Media management/registry/imedia_registry.h"

// Middle Bridge headers
#include "Middle Bridge/project/session_manager.h"
#include "Middle Bridge/project/project_lifecycle_controller.h"
#include "Middle Bridge/timeline/arrangement_controller.h"
#include "Middle Bridge/timeline/timeline_controller.h"
#include "Middle Bridge/tracks/track_controller.h"
#include "Middle Bridge/telemetry/metering_provider.h"
#include "Middle Bridge/engine/hardware_settings_facade.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <memory>
#include <limits>
#include <cstring>

using namespace Layer1;
using namespace Layer2;
using namespace Layer3;
using namespace DSP;

int main() {
    std::cout << "==========================================================" << std::endl;
    std::cout << "Middle Bridge Facades & Real-Time Playback Diagnostics" << std::endl;
    std::cout << "==========================================================" << std::endl;

    auto fs = IFileSystem::create();
    const char* audioPath = "/Users/goldenfung/Documents/agent-based-daw/tests/audio-for-test/Floor tom 3.03_12.wav";

    if (!fs->exists(audioPath)) {
        std::cerr << "Error: File does not exist at path: " << audioPath << std::endl;
        return 1;
    }

    // Create low-level core audio driver & engine
    auto audioDriver = Layer1::IAudioDriver::create(Layer1::AudioAPI::CORE_AUDIO);
    if (!audioDriver) {
        std::cout << "[Warning] CoreAudio Driver not available, falling back to SHARED auto-detect..." << std::endl;
        audioDriver = Layer1::IAudioDriver::create(Layer1::AudioAPI::SHARED);
    }
    if (!audioDriver) {
        std::cerr << "Error: Failed to instantiate AudioDriver" << std::endl;
        return 1;
    }

    auto audioEngine = Layer3::IAudioEngine::create();
    if (!audioEngine) {
        std::cerr << "Error: Failed to instantiate AudioEngine" << std::endl;
        return 1;
    }

    auto* butler = audioEngine->getButlerThread();
    if (!butler) {
        std::cerr << "Error: Engine did not instantiate butler thread" << std::endl;
        return 1;
    }
    butler->attachFileSystem(fs.get());

    // Create the Settings Facade to enumerate playback devices
    bridge::HardwareSettingsFacade settingsFacade(audioDriver.get(), nullptr, audioEngine.get());

    auto devices = settingsFacade.getAvailableDevices();
    std::cout << "\n--- Available Audio Playback Devices ---" << std::endl;
    for (const auto& dev : devices) {
        std::cout << "  [" << dev.deviceIndex << "] " << dev.name 
                  << " (Channels: In=" << dev.maxInputChannels 
                  << ", Out=" << dev.maxOutputChannels << ")";
        if (dev.isDefaultOutput) std::cout << " [DEFAULT OUTPUT]";
        std::cout << std::endl;
    }

    uint32_t chosenDeviceIndex = 0xFFFFFFFF;
    std::cout << "\nChoose an Output Device Index for playback: ";
    if (!(std::cin >> chosenDeviceIndex)) {
        std::cerr << "Error: Invalid selection input" << std::endl;
        return 1;
    }

    bool validSelection = false;
    for (const auto& dev : devices) {
        if (dev.deviceIndex == chosenDeviceIndex) {
            validSelection = true;
            break;
        }
    }
    if (!validSelection) {
        std::cerr << "Error: Device index " << chosenDeviceIndex << " does not exist" << std::endl;
        return 1;
    }

    // Build core scheduling infrastructure
    auto strings = IStringRegistry::create();
    auto mediaRegistry = MediaManagement::IMediaRegistry::create();
    auto kernel = ::IDSPKernel::create(256);
    auto bridgeMut = IMutationBridge::create();
    auto telemetryBridge = ITelemetryBridge::create();

    kernel->attachMutationBridge(bridgeMut.get());
    kernel->attachTelemetryBridge(telemetryBridge.get());

    // Register factories in DSP Kernel
    AudioSequencerFactory audioSequencerFactory;
    MidiSequencerFactory midiSequencerFactory;
    AudioSequencerFactory::setButlerThread(butler);
    AudioSequencerFactory::setFileSystem(fs.get());

    LatencyFactory latencyFactory;
    BusFactory busFactory;
    ChannelStripFactory channelStripFactory;
    PannerFactory pannerFactory;
    SendFactory sendFactory;

    AnalysisFactory analysisFactory;

    kernel->registerFactory(NODE_TYPE_AUDIO_SEQUENCER, &audioSequencerFactory);
    kernel->registerFactory(NODE_TYPE_MIDI_SEQUENCER, &midiSequencerFactory);
    kernel->registerFactory(NODE_TYPE_LATENCY, &latencyFactory);
    kernel->registerFactory(NODE_TYPE_BUS, &busFactory);
    kernel->registerFactory(NODE_TYPE_CHANNEL_STRIP, &channelStripFactory);
    kernel->registerFactory(NODE_TYPE_PANNER, &pannerFactory);
    kernel->registerFactory(NODE_TYPE_SEND, &sendFactory);

    kernel->registerFactory(NODE_TYPE_ANALYSIS, &analysisFactory);

    // Register processors in DSP Kernel
    kernel->registerProcessor(NODE_TYPE_AUDIO_SEQUENCER, processAudioSequencer);
    kernel->registerProcessor(NODE_TYPE_MIDI_SEQUENCER, processMidiSequencer);
    kernel->registerProcessor(NODE_TYPE_LATENCY, processLatency);
    kernel->registerProcessor(NODE_TYPE_BUS, processBus);
    kernel->registerProcessor(NODE_TYPE_CHANNEL_STRIP, processChannelStrip);
    kernel->registerProcessor(NODE_TYPE_PANNER, processPanner);
    kernel->registerProcessor(NODE_TYPE_SEND, processSend);

    kernel->registerProcessor(NODE_TYPE_ANALYSIS, processAnalysis);

    // Setup Clock, Tempo & Transport Services
    auto clockService = Layer2::IClockService::create();
    auto tempoService = Layer2::ITempoService::create();
    auto transport = Layer3::ITransport::create(44100);
    transport->setTempoService(tempoService.get());

    audioEngine->setScheduler(kernel.get());
    audioEngine->setTransport(transport.get());
    audioEngine->setClockService(clockService.get());
    audioEngine->setMutationBridge(bridgeMut.get());
    audioEngine->setFileSystem(fs.get());
    audioEngine->setTempoService(tempoService.get());

    // Instantiate Middle Bridge Facade components
    bridge::SessionManager sessionManager;
    
    bridge::ProjectLifecycleController lifecycleController(
        &sessionManager,
        kernel.get(),
        &audioSequencerFactory,
        &midiSequencerFactory,
        &latencyFactory,
        &channelStripFactory,
        &pannerFactory,
        &sendFactory,
        nullptr, // Plugin slot factory
        fs.get(),
        nullptr, // Plugin manager
        audioDriver.get(),
        butler
    );

    bridge::ArrangementController arrangementController(
        &sessionManager,
        strings.get(),
        mediaRegistry.get(),
        nullptr, // Waveform renderer stubbed
        fs.get()
    );

    bridge::TimelineController timelineController(
        transport.get(),
        tempoService.get()
    );

    bridge::MeteringProvider meteringProvider(
        telemetryBridge.get(),
        &sessionManager
    );

    bridge::TrackController trackController(
        &sessionManager,
        bridgeMut.get(),
        strings.get(),
        &meteringProvider
    );

    // Start background butler Refill Thread
    butler->start();

    // 1. Create a New Project Session via ProjectLifecycleController
    std::cout << "\nCreating a new Arranger Project Session via ProjectLifecycleController..." << std::endl;
    bridge::ProjectMetadataState projectMeta{};
    std::strcpy(projectMeta.projectName, "Epic Facade Session");
    std::strcpy(projectMeta.author, "Middle Bridge Producer");
    projectMeta.sampleRate = 44100;
    projectMeta.initialTempoBPM = 120.0f;
    projectMeta.timeSignatureNumerator = 4;
    projectMeta.timeSignatureDenominator = 4;

    if (!lifecycleController.createNewProject(projectMeta)) {
        std::cerr << "Error: Failed to create project session" << std::endl;
        return 1;
    }
    std::cout << "Project Session successfully initialized!" << std::endl;

    // 2. Add Tracks via TrackController
    std::cout << "\nCreating Tracks via TrackController..." << std::endl;
    TrackID audioTrackId = trackController.addAudioTrack("Lead Vocals Track", 2, 0xFF4CD964);
    TrackID monitorTrackId = trackController.addAuxTrack("Master Monitor FX", 0xFF00FFFF);

    if (!audioTrackId.isValid() || !monitorTrackId.isValid()) {
        std::cerr << "Error: Failed to instantiate project tracks" << std::endl;
        return 1;
    }

    // Retrieve active pipeline descriptors from TrackManager inside the Arranger Session
    auto* trackManager = sessionManager.getActiveSession()->getTrackManager();
    auto audioDesc = trackManager->getPipelineDescriptor(audioTrackId);
    auto monitorDesc = trackManager->getPipelineDescriptor(monitorTrackId);

    // 3. Construct Global routing & sum topology
    std::cout << "Wiring DSP Topology summing busses..." << std::endl;
    uint32_t nextNodeIndex = 0;

    // A. Master Sum Bus & Output Analysis Node
    NodeID masterBusId = busFactory.createNode();
    uint32_t masterBusIndex = nextNodeIndex++;
    {
        SystemMutation m{};
        m.type = MutationType::NODE_ADD;
        m.node.type = NODE_TYPE_BUS;
        m.node.id = masterBusId;
        bridgeMut->pushMutation(m);
    }

    NodeID analysisId = analysisFactory.createNode();
    uint32_t analysisIndex = nextNodeIndex++;
    {
        SystemMutation m{};
        m.type = MutationType::NODE_ADD;
        m.node.type = NODE_TYPE_ANALYSIS;
        m.node.id = analysisId;
        bridgeMut->pushMutation(m);
    }

    // B. Monitor Aux Send Bus Node
    NodeID monitorBusId = busFactory.createNode();
    uint32_t monitorBusIndex = nextNodeIndex++;
    {
        SystemMutation m{};
        m.type = MutationType::NODE_ADD;
        m.node.type = NODE_TYPE_BUS;
        m.node.id = monitorBusId;
        bridgeMut->pushMutation(m);
    }

    // C. Register AUX monitor track macro-node
    uint32_t monitorTrackIndex = nextNodeIndex++;
    {
        SystemMutation m{};
        m.type = MutationType::NODE_ADD;
        m.node.type = NODE_TYPE_AUDIO_TRACK;
        m.node.id = monitorDesc.trackNode;
        bridgeMut->pushMutation(m);
    }

    // D. Register Audio Track chain nodes
    uint32_t trackSequencerIndex = nextNodeIndex++;
    {
        SystemMutation m{};
        m.type = MutationType::NODE_ADD;
        m.node.type = NODE_TYPE_AUDIO_SEQUENCER;
        m.node.id = audioDesc.sourceNode;
        bridgeMut->pushMutation(m);
    }

    uint32_t trackNodeIndex = nextNodeIndex++;
    {
        SystemMutation m{};
        m.type = MutationType::NODE_ADD;
        m.node.type = NODE_TYPE_AUDIO_TRACK;
        m.node.id = audioDesc.trackNode;
        bridgeMut->pushMutation(m);
    }

    // Helper lambda to construct mutations for connecting nodes
    auto connectNodes = [&](uint32_t src, uint32_t dst, float gain = 1.0f) {
        SystemMutation m{};
        m.type = MutationType::NODE_CONNECT;
        m.connection.sourceNodeIndex = src;
        m.connection.destNodeIndex = dst;
        m.connection.gain = gain;
        bridgeMut->pushMutation(m);
    };

    // Connect sum buses to Master output
    connectNodes(monitorBusIndex, monitorTrackIndex);
    connectNodes(monitorTrackIndex, masterBusIndex);
    connectNodes(masterBusIndex, analysisIndex);

    // Wire Audio Track channels
    connectNodes(trackSequencerIndex, trackNodeIndex);
    connectNodes(trackNodeIndex, masterBusIndex);

    // Converge the DSP graph topology by driving dummy processes
    uint32_t initialVersion = kernel->getTopologyVersion();
    int timeout = 100;
    float *dummyPtrs[2] = {nullptr, nullptr};
    ProcessContext dummyContext{};
    dummyContext.isOffline = true;
    dummyContext.transportState = TransportState::PLAYING;

    while (kernel->getTopologyVersion() <= initialVersion && timeout-- > 0) {
        kernel->process(nullptr, dummyPtrs, 2, 0, &dummyContext);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (kernel->getNodeCount() != nextNodeIndex) {
        std::cerr << "Error: DSP topology failed to converge. Node count=" 
                  << kernel->getNodeCount() << ", expected=" << nextNodeIndex << std::endl;
        return 1;
    }
    std::cout << "DSP Topology successfully converged! Nodes loaded: " << kernel->getNodeCount() << std::endl;

    // Apply hardware audio configuration through Settings Facade (which opens and starts the stream)
    bridge::HardwareConfig hwConfig{};
    hwConfig.inputDeviceIndex = Layer1::UNUSED_DEVICE_INDEX; // Output only
    hwConfig.outputDeviceIndex = chosenDeviceIndex;
    hwConfig.numInputChannels = 0;
    hwConfig.numOutputChannels = 2;
    hwConfig.sampleRate = 44100;
    hwConfig.bufferSize = 512;

    std::cout << "\nOpening and starting audio hardware stream via HardwareSettingsFacade..." << std::endl;
    if (!settingsFacade.applyConfig(hwConfig)) {
        std::cerr << "Error: Failed to apply chosen hardware settings configuration" << std::endl;
        return 1;
    }

    // 4. Import WAV clip via ArrangementController
    std::cout << "\nImporting WAV clip via ArrangementController..." << std::endl;
    composition::RegionID regionId = arrangementController.importAudioClip(audioTrackId, audioPath, 0);
    if (!regionId.isValid()) {
        std::cerr << "Error: Failed to import WAV audio clip" << std::endl;
        return 1;
    }
    std::cout << "Audio clip imported and circular streaming buffer pre-filled successfully!" << std::endl;

    // Adjust track level slightly via TrackController fader fader
    trackController.setFaderGain(audioTrackId, 1.0f);

    // 5. Activate playback via TimelineController
    timelineController.play();

    std::cout << "\n>>> PLAYBACK ACTIVE (FACADE LIFE CYCLE) <<<" << std::endl;
    std::cout << "Signal out: Sampler -> Pre-Send (-6dB) -> Monitor Bus -> Master" << std::endl;
    std::cout << "Signal out: Sampler -> Track Channel Strip -> Master" << std::endl;
    std::cout << "Press [ENTER] to gracefully stop playback, close the project session, and exit..." << std::endl;

    // Flush and block until user hits Enter
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cin.get();

    std::cout << "\nTerminating playback stream..." << std::endl;
    timelineController.stop();
    (void)audioDriver->stopStream();
    audioDriver->closeStream();

    // 6. Close project via ProjectLifecycleController
    std::cout << "Closing project session via ProjectLifecycleController..." << std::endl;
    lifecycleController.closeProject();

    // Shutdown butler thread
    butler->stop();

    std::cout << "Graceful facade exit complete. Goodbye!" << std::endl;
    return 0;
}
