#include "Core audio engine/scheduler/idsp_kernel.h"
#include "Core audio engine/streaming/ibutler_thread.h"
#include "Core infrastructure/bridges/imutation_bridge.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "DSP nodes/analysis/analysis_node.h"
#include "DSP nodes/buses/bus_node.h"
#include "DSP nodes/channelstrip/channel_strip_node.h"
#include "DSP nodes/latency/latency_node.h"
#include "DSP nodes/panner/panner_node.h"
#include "DSP nodes/sampler/sampler_node.h"
#include "DSP nodes/tracks/audio_track_node.h"
#include "Hardware/OS abstraction/filesystem/codecs/wav_codec.h"
#include "Hardware/OS abstraction/filesystem/ifile_system.h"
#include "Media management/codecs/sndfile_reader.h"
#include "Media management/export/iexport_service.h"
#include "Media management/registry/imedia_registry.h"
#include "common/math/analysis.h"
#include "common/math/panning.h"
#include "musical_composition/interfaces/itrack_pipeline_builder.h"
#include "musical_composition/track_manager/track_manager_impl.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using namespace Layer1;
using namespace Layer2;
using namespace Layer3;
using namespace MediaManagement;
using namespace DSP;

struct TestTrackConfig {
  std::string path;
  FileHandle handle = INVALID_FILE_HANDLE;
  uint64_t totalFrames = 0;
  bool hasSend = false;
  uint32_t channels = 0;
  uint32_t sampleRate = 0;
};

class TestTrackPipelineBuilder : public composition::ITrackPipelineBuilder {
public:
  TestTrackPipelineBuilder(std::vector<TestTrackConfig> configs,
                           SamplerFactory *samplerF, LatencyFactory *latencyF,
                           PannerFactory *panF, SendFactory *sendF,
                           DSP::AudioTrackFactory *audioTrackF, IFileSystem *fs)
      : configs_(std::move(configs)), samplerF_(samplerF), latencyF_(latencyF),
        panF_(panF), sendF_(sendF), audioTrackF_(audioTrackF), fs_(fs) {}

  composition::TrackPipelineDescriptor
  buildPipeline(const composition::TrackCreateInfo &info,
                ::IDSPKernel *kernel) override {
    (void)kernel;

    if (info.type == composition::TrackType::AUX) {
      // Monitor Track Setup
      NodeID trackNodeId = audioTrackF_->createNode();
      if (auto *s = audioTrackF_->getRegistry().get(trackNodeId)) {
        s->channelStrip.mute = true;             // Mute the monitor track fader!
        s->channelStrip.muteRamp.init(0.0f, 64); // Prevent transition leakage!
      }

      NodeID pannerId = panF_->createNode();
      if (auto *s = panF_->getRegistry().get(pannerId)) {
        s->reset(44100.0f);
        s->targetPan = 0.5f;
        s->targetWidth = 1.0f;
        s->mode = 1;
      }

      composition::TrackPipelineDescriptor desc{};
      desc.trackNode = trackNodeId;
      return desc;
    }

    REQUIRE(nextTrackIndex_ < configs_.size());
    auto &config = configs_[nextTrackIndex_++];

    auto buffer = IStreamingBuffer::create(config.channels, config.sampleRate);
    buffer->setBufferSize(1000000);
    buffer->setReadAheadSize(1000000);
    buffer->associateFile(config.handle);

    uint64_t targetFrames = std::min(
        config.totalFrames, static_cast<uint64_t>(buffer->getTotalCapacity()));
    while (buffer->getAvailableFrames() < targetFrames) {
      buffer->requestRefill(buffer->getReadPosition());
      buffer->refillAsync(buffer->getReadPosition(), fs_);
    }
    buffer->setReadAheadSize(250000);

    // 1. Sampler Node (Source)
    NodeID samplerId = samplerF_->createNode();
    samplerF_->setBuffer(samplerId, buffer.get());
    samplerF_->setPlaybackState(samplerId, true);

    // 2. Latency Node (PDC) - Used for reference tracking in test
    NodeID latencyId = latencyF_->createNode();
    latencyF_->setLatency(latencyId, 256); // 256 samples PDC
    latencyNodes_.push_back(latencyId);

    // 3. Pre-Fader Send Panner Node
    NodeID preSendPannerId = panF_->createNode();
    if (auto *s = panF_->getRegistry().get(preSendPannerId)) {
      s->reset(static_cast<float>(config.sampleRate));
      s->targetPan = 0.5f; // Centered by default
      s->targetWidth = 1.0f;
      s->mode = 1; // Linear Balance
    }
    preSendPannerNodes_.push_back(preSendPannerId);

    // 4. Monolithic Audio Track Macro-Node
    NodeID trackNodeId = audioTrackF_->createNode();
    if (auto *s = audioTrackF_->getRegistry().get(trackNodeId)) {
      s->channelStrip.targetGain = 1.0f;
      s->channelStrip.targetPan = 0.5f;
    }
    audioTrackF_->setLatency(trackNodeId, 256); // Configure internal PDC

    // 5. Panner Node
    NodeID pannerId = panF_->createNode();
    if (auto *s = panF_->getRegistry().get(pannerId)) {
      s->reset(static_cast<float>(config.sampleRate));
      s->targetPan = 0.5f;
      s->targetWidth = 1.0f;
      s->mode = 1; // Linear Balance
    }

    // 6. Post-Fader Send Node
    NodeID postSendId = NodeID::invalid();
    if (config.hasSend) {
      postSendId = sendF_->createNode();
      if (auto *s = sendF_->getRegistry().get(postSendId)) {
        s->reset(static_cast<float>(config.sampleRate));
        s->targetGain = 0.5f; // -6dB send
        s->gainSmoother.init(0.5f, 10.0f,
                             static_cast<float>(config.sampleRate));
      }
    }

    // Store circular buffer for synchronous refill in tests
    buffers_.push_back(std::move(buffer));

    composition::TrackPipelineDescriptor desc{};
    desc.sourceNode = samplerId;
    desc.trackNode = trackNodeId;
    desc.latencySamples = 256;

    return desc;
  }

  void destroyPipeline(const composition::TrackPipelineDescriptor &pipeline,
                       ::IDSPKernel *kernel) override {
    (void)pipeline;
    (void)kernel;
  }

  // Allow test to access circular buffers
  std::vector<std::unique_ptr<IStreamingBuffer>> &getBuffers() {
    return buffers_;
  }

  std::vector<NodeID> &getLatencyNodes() { return latencyNodes_; }

  std::vector<NodeID> &getPreSendPannerNodes() { return preSendPannerNodes_; }

  IStreamingBuffer *getBuffer(size_t index) const {
    if (index < buffers_.size()) {
      return buffers_[index].get();
    }
    return nullptr;
  }

private:
  std::vector<TestTrackConfig> configs_;
  size_t nextTrackIndex_ = 0;
  SamplerFactory *samplerF_;
  LatencyFactory *latencyF_;
  PannerFactory *panF_;
  SendFactory *sendF_;
  DSP::AudioTrackFactory *audioTrackF_;
  IFileSystem *fs_;
  std::vector<std::unique_ptr<IStreamingBuffer>> buffers_;
  std::vector<NodeID> latencyNodes_;
  std::vector<NodeID> preSendPannerNodes_;
};

TEST_CASE("Real-World Audio Summing: Multi-Input Mix",
          "[Layer6][Export][Real][Summing]") {
  std::vector<std::string> inputPaths = {
      "/Users/goldenfung/Documents/agent-based-daw/tests/audio-for-test/Floor tom 3.02_11.wav",
      "/Users/goldenfung/Documents/agent-based-daw/tests/audio-for-test/Floor tom 3.03_12.wav",
      "/Users/goldenfung/Documents/agent-based-daw/tests/audio-for-test/Floor tom 3.06_18.wav",
      "/Users/goldenfung/Documents/agent-based-daw/tests/audio-for-test/Floor tom 3.07_24.wav",
      "/Users/goldenfung/Documents/agent-based-daw/tests/audio-for-test/Floor tom 3_02.wav",
      "/Users/goldenfung/Documents/agent-based-daw/tests/audio-for-test/Floor tom 3_03.wav"};
  const char *outputPath = "multi_floor_tom_summed.wav";

  auto fs = IFileSystem::create();
  auto strings = IStringRegistry::create();
  auto registry = IMediaRegistry::create();
  auto butler = IButlerThread::create();
  auto kernel = ::IDSPKernel::create();

  std::cout << "\n[1/5] Verifying Input Files..." << std::endl;

  uint32_t commonSampleRate = 0;
  uint32_t commonChannels = 0;
  uint64_t maxTotalSamples = 0;

  struct TrackPipeline {
    std::string path;
    FileHandle handle = INVALID_FILE_HANDLE;
    uint64_t totalFrames = 0;
    IStreamingBuffer *buffer = nullptr;

    // Track Pipeline Nodes (ordered logically: sampler -> latency -> preSendPanner -> channelStrip -> panner -> postSend)
    NodeID samplerId = NodeID::invalid();
    NodeID latencyId = NodeID::invalid();
    NodeID preSendPannerId = NodeID::invalid();
    NodeID channelStripId = NodeID::invalid();
    NodeID pannerId = NodeID::invalid();
    NodeID postSendId = NodeID::invalid();
  };
  std::vector<TrackPipeline> tracks;

  for (const auto &path : inputPaths) {
    REQUIRE(fs->exists(path.c_str()));
    FileHandle hInput = fs->openFile(path.c_str(), true);
    REQUIRE(hInput != INVALID_FILE_HANDLE);

    uint64_t fileSize = fs->getFileSize(hInput);
    REQUIRE(fileSize > 44);

    std::vector<uint8_t> headerBuf(65536);
    fs->readFileSync(hInput, 0, headerBuf.data(), 65536);

    WAVCodec::Header header;
    REQUIRE(WAVCodec::readHeader(headerBuf.data(), header));

    std::cout << "  > Path: " << path << std::endl;
    std::cout << "    Channels: " << header.numChannels
              << ", Sample Rate: " << header.sampleRate << " Hz"
              << ", Bit Depth: " << header.bitsPerSample << "-bit" << std::endl;

    if (commonSampleRate == 0) {
      commonSampleRate = header.sampleRate;
      commonChannels = header.numChannels;
    } else {
      REQUIRE(header.sampleRate == commonSampleRate);
      REQUIRE(header.numChannels == commonChannels);
    }

    uint64_t totalSamples = header.subchunk2Size /
                            (header.numChannels * (header.bitsPerSample / 8));
    std::cout << "    Total Frames: " << totalSamples << std::endl;

    maxTotalSamples = std::max(maxTotalSamples, totalSamples);

    TrackPipeline tp;
    tp.path = path;
    tp.handle = hInput;
    tp.totalFrames = totalSamples;
    tracks.push_back(std::move(tp));
  }

  std::cout << "  > Max Duration: " << maxTotalSamples << " frames"
            << std::endl;

  // 2. Setup Streaming & DSP Graph (Global Routing Topology)
  std::cout << "\n[2/5] Setting up Streaming & DSP Pipelines..." << std::endl;

  auto bridge = IMutationBridge::create();
  kernel->attachMutationBridge(bridge.get());

  kernel->registerProcessor(NODE_TYPE_SAMPLER, processSampler);
  kernel->registerProcessor(NODE_TYPE_LATENCY, processLatency);
  kernel->registerProcessor(NODE_TYPE_BUS, processBus);
  kernel->registerProcessor(NODE_TYPE_CHANNEL_STRIP, processChannelStrip);
  kernel->registerProcessor(NODE_TYPE_PANNER, processPanner);
  kernel->registerProcessor(NODE_TYPE_SEND, processSend);
  kernel->registerProcessor(NODE_TYPE_ANALYSIS, processAnalysis);

  SamplerFactory samplerFactory;
  LatencyFactory latencyFactory;
  BusFactory busFactory;
  ChannelStripFactory channelStripFactory;
  PannerFactory pannerFactory;
  SendFactory sendFactory;
  AnalysisFactory analysisFactory;
  DSP::AudioTrackFactory audioTrackFactory;
  kernel->registerProcessor(NODE_TYPE_AUDIO_TRACK, DSP::processAudioTrack);

  kernel->registerFactory(NODE_TYPE_SAMPLER, &samplerFactory);
  kernel->registerFactory(NODE_TYPE_LATENCY, &latencyFactory);
  kernel->registerFactory(NODE_TYPE_BUS, &busFactory);
  kernel->registerFactory(NODE_TYPE_CHANNEL_STRIP, &channelStripFactory);
  kernel->registerFactory(NODE_TYPE_PANNER, &pannerFactory);
  kernel->registerFactory(NODE_TYPE_SEND, &sendFactory);
  kernel->registerFactory(NODE_TYPE_ANALYSIS, &analysisFactory);
  kernel->registerFactory(NODE_TYPE_AUDIO_TRACK, &audioTrackFactory);

  SamplerFactory::setFileSystem(fs.get());

  // Step 2: Instantiate TrackManagerImpl at the Test's Composition Root
  std::vector<TestTrackConfig> testTrackConfigs;
  for (size_t i = 0; i < tracks.size(); ++i) {
    TestTrackConfig conf{};
    conf.path = tracks[i].path;
    conf.handle = tracks[i].handle;
    conf.totalFrames = tracks[i].totalFrames;
    conf.hasSend = (i == 0 || i == 3); // Match manual hasSend logic
    conf.channels = commonChannels;
    conf.sampleRate = commonSampleRate;
    testTrackConfigs.push_back(conf);
  }

  auto pipelineBuilder = std::make_unique<TestTrackPipelineBuilder>(
      std::move(testTrackConfigs), &samplerFactory, &latencyFactory,
      &pannerFactory, &sendFactory, &audioTrackFactory, fs.get());
  auto *builderPtr = pipelineBuilder.get();

  auto trackManager = std::make_unique<composition::TrackManagerImpl>(
      std::move(pipelineBuilder),
      nullptr, // No command history
      nullptr, // No source manager
      kernel.get(),
      nullptr,
      nullptr,
      NodeID::invalid(),
      NodeID::invalid(), // masterPluginSlotNode
      NodeID::invalid(), // masterLatencyNode
      &latencyFactory);

  // Step 3: Replace Manual Node Creation Loops with createTrack
  std::vector<TrackID> trackIds;
  for (size_t i = 0; i < tracks.size(); ++i) {
    composition::TrackCreateInfo info{};
    info.type = composition::TrackType::AUDIO;
    info.nameId = 0;
    info.colorARGB = 0;
    info.audioChannelCount = commonChannels;

    TrackID trackId = trackManager->createTrack(info);
    trackIds.push_back(trackId);

    // Retrieve the created pipeline descriptor
    auto desc = trackManager->getPipelineDescriptor(trackId);

    // Copy the constructed node IDs back to keep the existing test code
    // completely happy
    tracks[i].samplerId = desc.sourceNode;
    tracks[i].latencyId = builderPtr->getLatencyNodes()[i];
    tracks[i].channelStripId = desc.trackNode;
    tracks[i].pannerId = desc.trackNode;
    tracks[i].buffer = builderPtr->getBuffer(i);
  }

  // Create Monitor Track (AUX)
  composition::TrackCreateInfo monitorTrackInfo{};
  monitorTrackInfo.type = composition::TrackType::AUX;
  monitorTrackInfo.nameId = 0;
  monitorTrackInfo.colorARGB = 0xFF00FFFF;
  monitorTrackInfo.audioChannelCount = commonChannels;

  TrackID monitorTrackId = trackManager->createTrack(monitorTrackInfo);
  auto monitorDesc = trackManager->getPipelineDescriptor(monitorTrackId);

  std::cout << "  > All Track Pipelines: READY and FULL" << std::endl;

  std::cout << "\n[3/5] Constructing Global Routing DAG..." << std::endl;

  uint32_t nextNodeIndex = 0;

  // 2. Master Bus & Analysis
  NodeID masterBusId = busFactory.createNode();
  uint32_t masterBusIndex = nextNodeIndex++;
  {
    SystemMutation m{};
    m.type = MutationType::NODE_ADD;
    m.node.type = NODE_TYPE_BUS;
    m.node.id = masterBusId;
    bridge->pushMutation(m);
  }

  NodeID analysisId = analysisFactory.createNode();
  uint32_t analysisIndex = nextNodeIndex++;
  {
    SystemMutation m{};
    m.type = MutationType::NODE_ADD;
    m.node.type = NODE_TYPE_ANALYSIS;
    m.node.id = analysisId;
    bridge->pushMutation(m);
  }

  // 3. Drum Bus Group
  NodeID drumBusId = busFactory.createNode();
  uint32_t drumBusIndex = nextNodeIndex++;
  {
    SystemMutation m{};
    m.type = MutationType::NODE_ADD;
    m.node.type = NODE_TYPE_BUS;
    m.node.id = drumBusId;
    bridge->pushMutation(m);
  }

  NodeID drumCsId = channelStripFactory.createNode();
  uint32_t drumCsIndex = nextNodeIndex++;
  {
    if (auto *s = channelStripFactory.getRegistry().get(drumCsId)) {
      s->reset(static_cast<float>(commonSampleRate));
    }
    SystemMutation m{};
    m.type = MutationType::NODE_ADD;
    m.node.type = NODE_TYPE_CHANNEL_STRIP;
    m.node.id = drumCsId;
    bridge->pushMutation(m);
  }

  NodeID drumPanId = pannerFactory.createNode();
  uint32_t drumPanIndex = nextNodeIndex++;
  {
    if (auto *s = pannerFactory.getRegistry().get(drumPanId)) {
      s->reset(static_cast<float>(commonSampleRate));
      s->mode = 1;
    }
    SystemMutation m{};
    m.type = MutationType::NODE_ADD;
    m.node.type = NODE_TYPE_PANNER;
    m.node.id = drumPanId;
    bridge->pushMutation(m);
  }

  // 4. Reverb FX Bus
  NodeID reverbBusId = busFactory.createNode();
  uint32_t reverbBusIndex = nextNodeIndex++;
  {
    SystemMutation m{};
    m.type = MutationType::NODE_ADD;
    m.node.type = NODE_TYPE_BUS;
    m.node.id = reverbBusId;
    bridge->pushMutation(m);
  }

  NodeID reverbCsId = channelStripFactory.createNode();
  uint32_t reverbCsIndex = nextNodeIndex++;
  {
    if (auto *s = channelStripFactory.getRegistry().get(reverbCsId)) {
      s->reset(static_cast<float>(commonSampleRate));
    }
    SystemMutation m{};
    m.type = MutationType::NODE_ADD;
    m.node.type = NODE_TYPE_CHANNEL_STRIP;
    m.node.id = reverbCsId;
    bridge->pushMutation(m);
  }

  // 4b. Monitor Bus & Monitor Track Setup
  NodeID monitorBusId = busFactory.createNode();
  uint32_t monitorBusIndex = nextNodeIndex++;
  {
    SystemMutation m{};
    m.type = MutationType::NODE_ADD;
    m.node.type = NODE_TYPE_BUS;
    m.node.id = monitorBusId;
    bridge->pushMutation(m);
  }

  uint32_t monitorCsIndex = nextNodeIndex++;
  {
    SystemMutation m{};
    m.type = MutationType::NODE_ADD;
    m.node.type = NODE_TYPE_AUDIO_TRACK;
    m.node.id = monitorDesc.trackNode;
    bridge->pushMutation(m);
  }

  // 5. Track Chains
  struct TrackIndices {
    uint32_t sampler = 0;
    uint32_t cs = 0;
  };
  std::vector<TrackIndices> trackIndices(tracks.size());

  for (size_t i = 0; i < tracks.size(); ++i) {
    TrackID trackId = trackIds[i];
    auto desc = trackManager->getPipelineDescriptor(trackId);

    // 1. Sampler Node (Source)
    trackIndices[i].sampler = nextNodeIndex++;
    SystemMutation mSamp{};
    mSamp.type = MutationType::NODE_ADD;
    mSamp.node.type = NODE_TYPE_SAMPLER;
    mSamp.node.id = desc.sourceNode;
    bridge->pushMutation(mSamp);

    // 2. Monolithic Audio Track Macro-Node
    trackIndices[i].cs = nextNodeIndex++;
    SystemMutation mTrk{};
    mTrk.type = MutationType::NODE_ADD;
    mTrk.node.type = NODE_TYPE_AUDIO_TRACK;
    mTrk.node.id = desc.trackNode;
    bridge->pushMutation(mTrk);
  }

  auto connectNodes = [&](uint32_t src, uint32_t dst, float gain = 1.0f, uint32_t destPort = 0, uint32_t sourcePort = 0) {
    SystemMutation m{};
    m.type = MutationType::NODE_CONNECT;
    m.connection.sourceNodeIndex = src;
    m.connection.destNodeIndex = dst;
    m.connection.destPort = destPort;
    m.connection.sourcePort = sourcePort;
    m.connection.gain = gain;
    bridge->pushMutation(m);
  };

  // 6. Connect the Global Routing Topology
  float mixGain = 1.0f / static_cast<float>(tracks.size());

  for (uint32_t ch = 0; ch < commonChannels; ++ch) {
    connectNodes(drumBusIndex, drumCsIndex, 1.0f, ch, ch);
    connectNodes(drumCsIndex, drumPanIndex, 1.0f, ch, ch);
    connectNodes(drumPanIndex, masterBusIndex, 1.0f, ch, ch);

    connectNodes(reverbBusIndex, reverbCsIndex, 1.0f, ch, ch);
    connectNodes(reverbCsIndex, masterBusIndex, mixGain, ch, ch);

    // Connect the Monitor Track through the Monitor Track to the Master Bus
    connectNodes(monitorBusIndex, monitorCsIndex, 1.0f, ch, ch);
    connectNodes(monitorCsIndex, masterBusIndex, 1.0f, ch, ch);

    connectNodes(masterBusIndex, analysisIndex, 1.0f, ch, ch);
  }

  for (size_t i = 0; i < tracks.size(); ++i) {
    connectNodes(trackIndices[i].sampler, trackIndices[i].cs, 1.0f, TRACK_INPUT_PLAYBACK_PORT_BASE, 0);
    if (commonChannels >= 2) {
      connectNodes(trackIndices[i].sampler, trackIndices[i].cs, 1.0f, TRACK_INPUT_PLAYBACK_PORT_BASE + 1, 1);
    }

    for (uint32_t ch = 0; ch < commonChannels; ++ch) {
      if (i < 3) {
        connectNodes(trackIndices[i].cs, drumBusIndex, mixGain, ch, ch);
      } else {
        connectNodes(trackIndices[i].cs, masterBusIndex, mixGain, ch, ch);
      }
    }
  }

  // Trigger topology swap
  uint32_t initialVersion = kernel->getTopologyVersion();
  int timeout2 = 100;

  float *dummyPtrs[2] = {nullptr, nullptr};
  ProcessContext dummyContext{};
  dummyContext.isOffline = true;
  dummyContext.transportState = TransportState::PLAYING;

  while (kernel->getTopologyVersion() <= initialVersion && timeout2-- > 0) {
    kernel->process(nullptr, dummyPtrs, 2, 0, &dummyContext);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  REQUIRE(kernel->getNodeCount() == nextNodeIndex);

  std::cout << "  > Total Graph Latency: " << kernel->getTotalLatency()
            << " samples" << std::endl;
  REQUIRE(kernel->getTotalLatency() == 256);

  // 4. Setup Export
  std::cout << "\n[4/5] Executing Offline Render (Export)..." << std::endl;
  auto exportService =
      IExportService::create(registry.get(), strings.get(), kernel.get());

  ExportConfig config{};
  config.outputPathId = strings->registerString(outputPath);
  config.sampleRate = commonSampleRate;
  config.numChannels = static_cast<uint16_t>(commonChannels);
  config.startSample = 0;
  config.endSample = maxTotalSamples;
  config.format = ExportFormat::WAV;
  config.bitDepth = ExportBitDepth::BIT_24;
  config.normalize = false;

  bool completed = false;
  bool success = false;
  struct ExportContext {
    bool *completed;
    bool *success;
  };
  ExportContext ctx = {&completed, &success};

  auto callback = [](uint64_t, bool s, const char *error, void *c) {
    auto *context = static_cast<ExportContext *>(c);
    *context->completed = true;
    *context->success = s;
    if (!s && error)
      std::cout << "  > Render Callback Error: " << error << std::endl;
  };

  uint64_t jobId = exportService->exportRangeAsync(config, callback, &ctx);
  std::cout << "  > Job Started: ID=" << jobId << std::endl;

  // Wait for export
  float lastProgress = -1.0f;
  for (int i = 0; i < 2000; ++i) {
    exportService->update();
    // Step 5: Adapt the Offline Render Loop for Synchronous Refills
    for (size_t trackIdx = 0; trackIdx < trackIds.size(); ++trackIdx) {
      auto *buf = builderPtr->getBuffer(trackIdx);
      if (buf) {
        buf->requestRefill(buf->getReadPosition());
        buf->refillAsync(buf->getReadPosition(), fs.get());
      }
    }

    ExportProgress progress;
    if (exportService->getProgress(jobId, progress)) {
      if (progress.progress != lastProgress) {
        std::cout << "  > Progress: "
                  << static_cast<int>(progress.progress * 100) << "%"
                  << std::endl;
        lastProgress = progress.progress;
      }
    }

    if (completed)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  REQUIRE(completed);
  REQUIRE(success);
  std::cout << "  > Render COMPLETE: Success" << std::endl;

  // 5. Verify Output File
  std::cout << "\n[5/5] Verifying Result..." << std::endl;
  REQUIRE(fs->exists(outputPath));

  SndFileReader reader(outputPath);
  REQUIRE(reader.isValid());

  std::cout << "  > Output Path: " << outputPath << std::endl;
  std::cout << "  > Output Channels: " << reader.getNumChannels() << std::endl;
  std::cout << "  > Output Sample Rate: " << reader.getSampleRate() << " Hz"
            << std::endl;
  std::cout << "  > Output Frames: " << reader.getTotalFrames() << std::endl;

  REQUIRE(reader.getNumChannels() == commonChannels);
  REQUIRE(reader.getSampleRate() == commonSampleRate);
  REQUIRE(reader.getTotalFrames() >= maxTotalSamples);

  uint32_t totalSamplesInterleaved =
      static_cast<uint32_t>(maxTotalSamples * commonChannels);

  // Check that the output is NOT silent
  std::vector<float> readBuf(totalSamplesInterleaved);
  uint32_t readFrames =
      reader.readFrames(readBuf.data(), static_cast<uint32_t>(maxTotalSamples));
  REQUIRE(readFrames > 0);

  float maxAmp = 0.0f;
  for (uint32_t i = 0; i < totalSamplesInterleaved; ++i) {
    if (std::abs(readBuf[i]) > maxAmp)
      maxAmp = std::abs(readBuf[i]);
  }
  std::cout << "  > Output Max Amplitude: " << maxAmp << std::endl;
  REQUIRE(maxAmp > 0.0001f); // Must have some signal

  // 6. Signal Analysis Comparison
  std::cout
      << "\n[6/5] Performing Signal Analysis Comparison (Mathematical Sum)..."
      << std::endl;

  // Pre-calculate expected mathematical sum from all sources
  std::vector<float> expectedSumBuf(totalSamplesInterleaved, 0.0f);

  uint64_t latencyFrames = 256;

  float csLeft, csRight;
  Math::Panning::calculateEqualPower(0.5f, csLeft, csRight);

  float panLeft, panRight;
  Math::Panning::calculateLinear(0.5f, panLeft, panRight);

  // Load all source buffers first for direct access
  std::vector<std::vector<float>> sourceBuffers(tracks.size());
  for (size_t i = 0; i < tracks.size(); ++i) {
    auto &track = tracks[i];
    SndFileReader sourceReader(track.path.c_str());
    REQUIRE(sourceReader.isValid());
    track.totalFrames = sourceReader.getTotalFrames();
    sourceBuffers[i].resize(track.totalFrames * commonChannels);
    sourceReader.readFrames(sourceBuffers[i].data(), static_cast<uint32_t>(track.totalFrames));
  }

  // Calculate the exact summed outputs frame-by-frame
  for (uint64_t f = 0; f < maxTotalSamples; ++f) {
    if (f + latencyFrames >= maxTotalSamples)
      break;

    float drumBusL = 0.0f;
    float drumBusR = 0.0f;
    float masterBusL = 0.0f;
    float masterBusR = 0.0f;
    float reverbBusL = 0.0f;
    float reverbBusR = 0.0f;
    // Monitor Bus calculations are omitted since the monitor track fader is muted

    for (size_t i = 0; i < tracks.size(); ++i) {
      if (f >= tracks[i].totalFrames)
        continue;

      float currentGain = 1.0f;

      float inL = sourceBuffers[i][f * commonChannels + 0];
      float inR = sourceBuffers[i][f * commonChannels + (commonChannels >= 2 ? 1 : 0)];

      // A. Track Channel Strip (gain + equal power panning)
      float csOutL = inL * currentGain * csLeft;
      float csOutR = inR * currentGain * csRight;

      // E. Route track main output to either Drum Bus or Master Bus
      if (i < 3) {
        drumBusL += csOutL * mixGain;
        drumBusR += csOutR * mixGain;
      } else {
        masterBusL += csOutL * mixGain;
        masterBusR += csOutR * mixGain;
      }
    }

    // F. Process Drum Bus Strip & Panner -> Master Bus
    float drumCsL = drumBusL * csLeft;
    float drumCsR = drumBusR * csRight;
    float drumPanL = drumCsL * panLeft;
    float drumPanR = drumCsR * panRight;

    masterBusL += drumPanL;
    masterBusR += drumPanR;

    // G. Process Reverb Bus Strip -> Master Bus
    float reverbCsL = reverbBusL * csLeft;
    float reverbCsR = reverbBusR * csRight;

    masterBusL += reverbCsL * mixGain;
    masterBusR += reverbCsR * mixGain;

    // H. Process Monitor Track Strip & Panner -> Master Bus
    // Note: The Monitor Track fader is MUTED in the test setup (s->mute = true)
    // to prevent transition leakage. Thus, its actual contribution to the Master Bus is 0.0f.

    // Write final output to expected buffer
    expectedSumBuf[(f + latencyFrames) * commonChannels + 0] = masterBusL;
    if (commonChannels >= 2) {
      expectedSumBuf[(f + latencyFrames) * commonChannels + 1] = masterBusR;
    }
  }

  // Load actual output buffer
  std::vector<float> outputBuf(totalSamplesInterleaved);
  reader.seek(0);
  reader.readFrames(outputBuf.data(), static_cast<uint32_t>(maxTotalSamples));

  // Diagnostic: Find the first non-zero sample in both expected and actual to
  // detect shift
  int64_t firstExpected = -1, firstOutput = -1;
  for (uint32_t i = 0; i < totalSamplesInterleaved; ++i) {
    if (std::abs(expectedSumBuf[i]) > 0.0001f && firstExpected == -1)
      firstExpected = static_cast<int64_t>(i);
    if (std::abs(outputBuf[i]) > 0.0001f && firstOutput == -1)
      firstOutput = static_cast<int64_t>(i);
  }

  std::cout << "  > First Audio Index: Expected=" << firstExpected
            << ", Output=" << firstOutput << std::endl;

  // Print non-zero samples before latency mark if any exist
  for (uint32_t i = 0; i < 256 && i < totalSamplesInterleaved; ++i) {
    if (std::abs(outputBuf[i]) > 1e-6f) {
      std::cout << "    [Pre-Latency Output sample at i=" << i << "]: " << outputBuf[i] << std::endl;
    }
  }

  // The graph latency is exactly 256 samples
  uint32_t offsetExpected = static_cast<uint32_t>(latencyFrames * commonChannels);
  uint32_t offsetOutput = static_cast<uint32_t>(latencyFrames * commonChannels);

    uint32_t validFrames =
        static_cast<uint32_t>(std::min(totalSamplesInterleaved - offsetExpected,
                                       totalSamplesInterleaved - offsetOutput));

    float correlation = Math::Analysis::calculateCorrelation(
        expectedSumBuf.data() + offsetExpected, outputBuf.data() + offsetOutput,
        validFrames);

    float rmsError = Math::Analysis::calculateRMSError(
        expectedSumBuf.data() + offsetExpected, outputBuf.data() + offsetOutput,
        validFrames);

    float maxError = Math::Analysis::calculateMaxAbsoluteError(
        expectedSumBuf.data() + offsetExpected, outputBuf.data() + offsetOutput,
        validFrames);

    std::cout << "  > Correlation (Aligned): " << correlation << std::endl;
    std::cout << "  > RMS Error (Aligned): " << rmsError << std::endl;
    std::cout << "  > Max Absolute Error (Aligned): " << maxError << std::endl;

    std::cout << "  > First 10 validFrames:" << std::endl;
    for (uint32_t i = 0; i < 10 && i < validFrames; ++i) {
      float exp = expectedSumBuf[offsetExpected + i];
      float act = outputBuf[offsetOutput + i];
      std::cout << "    [i=" << i << "] Exp: " << exp << ", Act: " << act << ", Diff: " << std::abs(exp - act) << std::endl;
    }
    std::cout << "  > validFrames 1275-1285:" << std::endl;
    for (uint32_t i = 1275; i <= 1285 && i < validFrames; ++i) {
      float exp = expectedSumBuf[offsetExpected + i];
      float act = outputBuf[offsetOutput + i];
      std::cout << "    [i=" << i << "] Exp: " << exp << ", Act: " << act << ", Diff: " << std::abs(exp - act) << std::endl;
    }

    int mismatches_printed = 0;
    for (uint32_t i = 0; i < validFrames && mismatches_printed < 10; ++i) {
      float exp = expectedSumBuf[offsetExpected + i];
      float act = outputBuf[offsetOutput + i];
      if (std::abs(exp - act) > 0.0001f) {
        std::cout << "  > Mismatch at validFrame " << i 
                  << " (Channel " << (i % commonChannels) << "): Expected " << exp 
                  << ", Actual " << act << " | Diff: " << std::abs(exp - act) << std::endl;
        mismatches_printed++;
      }
    }

    REQUIRE(correlation > 0.9999f);
    REQUIRE(rmsError < 0.0001f);

  std::cout << "\n>>> MULTI-INPUT SUMMING TEST SUCCESSFUL <<<\n" << std::endl;

  // Cleanup
  butler->stop();
  for (auto &track : tracks) {
    fs->closeFile(track.handle);
  }
}
