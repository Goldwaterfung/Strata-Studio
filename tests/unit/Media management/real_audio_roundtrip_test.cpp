#include "Core audio engine/scheduler/idsp_kernel.h"
#include "Core audio engine/streaming/ibutler_thread.h"
#include "Core infrastructure/bridges/imutation_bridge.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "DSP nodes/buses/bus_node.h"
#include "DSP nodes/latency/latency_node.h"
#include "DSP nodes/sampler/sampler_node.h"
#include "Hardware/OS abstraction/filesystem/codecs/wav_codec.h"
#include "Hardware/OS abstraction/filesystem/ifile_system.h"
#include "Media management/codecs/sndfile_reader.h"
#include "Media management/export/iexport_service.h"
#include "Media management/registry/imedia_registry.h"
#include "common/math/analysis.h"
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

TEST_CASE("Real-World Audio Round-Trip: Floor Tom", "[Layer6][Export][Real]") {
  const char *inputPath = "/Users/goldenfung/Documents/agent-based-daw/tests/"
                          "audio-for-test/Floor tom 3.03_12.wav";
  const char *outputPath = "floor_tom_roundtrip.wav";

  auto fs = IFileSystem::create();
  auto strings = IStringRegistry::create();
  auto registry = IMediaRegistry::create();
  auto butler = IButlerThread::create();
  auto kernel = IDSPKernel::create();

  std::cout << "\n[1/5] Verifying Input File..." << std::endl;
  REQUIRE(fs->exists(inputPath));
  FileHandle hInput = fs->openFile(inputPath, true);
  REQUIRE(hInput != INVALID_FILE_HANDLE);

  uint64_t fileSize = fs->getFileSize(hInput);
  REQUIRE(fileSize > 44);

  std::vector<uint8_t> headerBuf(65536);
  fs->readFileSync(hInput, 0, headerBuf.data(), 65536);

  WAVCodec::Header header;
  REQUIRE(WAVCodec::readHeader(headerBuf.data(), header));

  std::cout << "  > Path: " << inputPath << std::endl;
  std::cout << "  > Size: " << fileSize << " bytes" << std::endl;
  std::cout << "  > Channels: " << header.numChannels << std::endl;
  std::cout << "  > Sample Rate: " << header.sampleRate << " Hz" << std::endl;
  std::cout << "  > Bit Depth: " << header.bitsPerSample << "-bit" << std::endl;

  uint64_t totalSamples =
      header.subchunk2Size / (header.numChannels * (header.bitsPerSample / 8));
  std::cout << "  > Total Frames: " << totalSamples << std::endl;

  // 2. Setup Streaming
  std::cout << "\n[2/5] Setting up Streaming Pipeline..." << std::endl;
  butler->attachFileSystem(fs.get());
  butler->setSampleRate(static_cast<float>(header.sampleRate));
  butler->start();

  auto streamBuf =
      IStreamingBuffer::create(header.numChannels, header.sampleRate);
  streamBuf->setBufferSize(1000000);
  streamBuf->setReadAheadSize(1000000);
  streamBuf->associateFile(hInput);

  butler->registerBuffer(streamBuf.get());
  streamBuf->requestRefill(0);
  butler->wakeButler();

  // Wait for buffer to contain the whole file
  std::cout << "  > Waiting for buffer to fill (" << totalSamples
            << " frames)..." << std::endl;
  int retries = 500;
  while (streamBuf->getAvailableFrames() < totalSamples && retries-- > 0) {
    streamBuf->requestRefill(0);
    butler->wakeButler();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  REQUIRE(streamBuf->getAvailableFrames() >= totalSamples);
  std::cout << "  > Streaming Buffer: READY and FULL" << std::endl;

  // 3. Setup DSP Graph
  std::cout << "\n[3/5] Constructing DSP Graph..." << std::endl;
  auto bridge = IMutationBridge::create();
  kernel->attachMutationBridge(bridge.get());

  kernel->registerProcessor(NODE_TYPE_SAMPLER, processSampler);
  kernel->registerProcessor(NODE_TYPE_LATENCY, processLatency);
  kernel->registerProcessor(NODE_TYPE_BUS, processBus);

  SamplerFactory samplerFactory;
  LatencyFactory latencyFactory;
  BusFactory busFactory;
  kernel->registerFactory(NODE_TYPE_SAMPLER, &samplerFactory);
  kernel->registerFactory(NODE_TYPE_LATENCY, &latencyFactory);
  kernel->registerFactory(NODE_TYPE_BUS, &busFactory);
  SamplerFactory &factory = samplerFactory;
  NodeID samplerId = factory.createNode();
  factory.setBuffer(samplerId, streamBuf.get());
  factory.setPlaybackState(samplerId, true);

  NodeID latencyId = latencyFactory.createNode();
  latencyFactory.setLatency(latencyId, 512); // 512 samples of delay

  NodeID masterId = busFactory.createNode();

  std::cout << "  > Graph: Sampler [" << samplerId.id << "] -> LatencyNode ["
            << latencyId.id << "] -> Master [" << masterId.id << "]"
            << std::endl;

  // Node Add Mutations
  SystemMutation m1{}, m2{}, m3{};
  m1.type = MutationType::NODE_ADD;
  m1.node.type = NODE_TYPE_SAMPLER;
  m1.node.id = samplerId;
  bridge->pushMutation(m1);

  m2.type = MutationType::NODE_ADD;
  m2.node.type = NODE_TYPE_LATENCY;
  m2.node.id = latencyId;
  bridge->pushMutation(m2);

  m3.type = MutationType::NODE_ADD;
  m3.node.type = NODE_TYPE_BUS;
  m3.node.id = masterId;
  bridge->pushMutation(m3);

  // Connection Mutation: Sampler (0) -> Latency (1)
  SystemMutation mConn1{};
  mConn1.type = MutationType::NODE_CONNECT;
  mConn1.connection.sourceNodeIndex = 0;
  mConn1.connection.destNodeIndex = 1;
  mConn1.connection.gain = 1.0f;
  bridge->pushMutation(mConn1);

  // Connection Mutation: Latency (1) -> Master (2)
  SystemMutation mConn2{};
  mConn2.type = MutationType::NODE_CONNECT;
  mConn2.connection.sourceNodeIndex = 1;
  mConn2.connection.destNodeIndex = 2;
  mConn2.connection.gain = 1.0f;
  bridge->pushMutation(mConn2);

  // Trigger topology swap by calling process() (simulates audio thread)
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

  // Ensure nodes are actually in the active topology
  REQUIRE(kernel->getNodeCount() >= 3);

  std::cout << "  > Total Graph Latency: " << kernel->getTotalLatency()
            << " samples" << std::endl;
  REQUIRE(kernel->getTotalLatency() == 512);

  // 4. Setup Export
  std::cout << "\n[4/5] Executing Offline Render (Export)..." << std::endl;
  auto exportService =
      IExportService::create(registry.get(), strings.get(), kernel.get());

  ExportConfig config{};
  config.outputPathId = strings->registerString(outputPath);
  config.sampleRate = header.sampleRate;
  config.numChannels = header.numChannels;
  config.startSample = 0;
  config.endSample = totalSamples;
  config.format = ExportFormat::WAV;
  config.bitDepth = (header.bitsPerSample == 16) ? ExportBitDepth::BIT_16
                                                 : ExportBitDepth::BIT_24;
  config.normalize = false;

  bool completed = false;
  bool success = false;
  struct Context {
    bool *completed;
    bool *success;
  };
  Context ctx = {&completed, &success};

  auto callback2 = [](uint64_t, bool s, const char *error, void *c) {
    auto *context = static_cast<Context *>(c);
    *context->completed = true;
    *context->success = s;
    if (!s && error)
      std::cout << "  > Render Callback Error: " << error << std::endl;
  };

  uint64_t jobId = exportService->exportRangeAsync(config, callback2, &ctx);
  std::cout << "  > Job Started: ID=" << jobId << std::endl;

  // Wait for export
  float lastProgress = -1.0f;
  for (int i = 0; i < 2000; ++i) {
    exportService->update();
    if (streamBuf) {
      streamBuf->requestRefill(streamBuf->getReadPosition());
      streamBuf->refillAsync(streamBuf->getReadPosition(), fs.get());
    }
    butler->wakeButler(); // Ensure the butler refills buffers during offline
                          // render

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

  REQUIRE(reader.getNumChannels() == header.numChannels);
  REQUIRE(reader.getSampleRate() == header.sampleRate);
  REQUIRE(reader.getTotalFrames() >= totalSamples);

  uint32_t numChannels = header.numChannels;
  uint32_t totalSamplesInterleaved =
      static_cast<uint32_t>(totalSamples * numChannels);

  // Check that the output is NOT silent
  std::vector<float> readBuf(totalSamplesInterleaved);
  uint32_t readFrames =
      reader.readFrames(readBuf.data(), static_cast<uint32_t>(totalSamples));
  REQUIRE(readFrames > 0);

  float maxAmp = 0.0f;
  for (uint32_t i = 0; i < totalSamplesInterleaved; ++i) {
    if (std::abs(readBuf[i]) > maxAmp)
      maxAmp = std::abs(readBuf[i]);
  }
  std::cout << "  > Output Max Amplitude: " << maxAmp << std::endl;
  REQUIRE(maxAmp > 0.0001f); // Must have some signal

  // 6. Signal Analysis Comparison
  std::cout << "\n[6/5] Performing Signal Analysis Comparison..." << std::endl;

  // Load original source data for comparison
  SndFileReader sourceReader(inputPath);
  REQUIRE(sourceReader.isValid());

  std::vector<float> sourceBuf(totalSamplesInterleaved);
  sourceReader.readFrames(sourceBuf.data(),
                          static_cast<uint32_t>(totalSamples));

  // Ensure output buffer also contains all samples
  std::vector<float> outputBuf(totalSamplesInterleaved);
  reader.seek(0);
  reader.readFrames(outputBuf.data(), static_cast<uint32_t>(totalSamples));

  // Diagnostic: Find the first non-zero sample to detect shift
  int64_t firstSource = -1, firstOutput = -1;
  for (uint32_t i = 0; i < totalSamplesInterleaved; ++i) {
    if (std::abs(sourceBuf[i]) > 0.0001f && firstSource == -1)
      firstSource = static_cast<int64_t>(i);
    if (std::abs(outputBuf[i]) > 0.0001f && firstOutput == -1)
      firstOutput = static_cast<int64_t>(i);
  }

  std::cout << "  > First Audio Index: Source=" << firstSource
            << ", Output=" << firstOutput << std::endl;
  if (firstSource != -1 && firstOutput != -1) {
    std::cout << "  > Measured Shift: " << (firstOutput - firstSource)
              << " samples" << std::endl;
  }

  float correlation = 0.0f;
  float rmsError = 0.0f;
  float maxError = 0.0f;

  if (firstSource != -1 && firstOutput != -1) {
    uint32_t offsetSource = static_cast<uint32_t>(firstSource);
    uint32_t offsetOutput = static_cast<uint32_t>(firstOutput);
    uint32_t validFrames =
        static_cast<uint32_t>(std::min(totalSamplesInterleaved - offsetSource,
                                       totalSamplesInterleaved - offsetOutput));

    correlation = Math::Analysis::calculateCorrelation(
        sourceBuf.data() + offsetSource, outputBuf.data() + offsetOutput,
        validFrames);

    rmsError = Math::Analysis::calculateRMSError(
        sourceBuf.data() + offsetSource, outputBuf.data() + offsetOutput,
        validFrames);

    maxError = Math::Analysis::calculateMaxAbsoluteError(
        sourceBuf.data() + offsetSource, outputBuf.data() + offsetOutput,
        validFrames);
  }

  std::cout << "  > Correlation (Aligned): " << correlation << std::endl;
  std::cout << "  > RMS Error (Aligned): " << rmsError << std::endl;
  std::cout << "  > Max Absolute Error (Aligned): " << maxError << std::endl;

  if (correlation < 0.9999f) {
    std::cout << "\nMISMATCH DIAGNOSTICS (At alignment):" << std::endl;
    std::cout << "  > Source (at " << firstSource << "): ";
    for (uint32_t i = 0; i < 5; ++i)
      std::cout << sourceBuf[static_cast<uint32_t>(firstSource) + i] << " ";
    std::cout << "\n  > Output (at " << firstOutput << "): ";
    for (uint32_t i = 0; i < 5; ++i)
      std::cout << outputBuf[static_cast<uint32_t>(firstOutput) + i] << " ";
    std::cout << std::endl;
  }

  // For a PDC round-trip, we expect very high correlation on the aligned
  // signal.
  REQUIRE(correlation > 0.9999f);
  REQUIRE(rmsError < 0.0001f);

  std::cout << "\n>>> ROUND-TRIP TEST SUCCESSFUL <<<\n" << std::endl;

  // Cleanup
  butler->stop();
  fs->closeFile(hInput);
}
