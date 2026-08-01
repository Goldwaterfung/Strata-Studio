#pragma once
#include "musical_composition/interfaces/itrack_pipeline_builder.h"
#include <memory>
#include <vector>

// Forward declarations from Layer 4, Layer 3, Layer 1, Layer 2
namespace DSP {
class SamplerFactory;
class LatencyFactory;
class ChannelStripFactory;
class PannerFactory;
class SendFactory;
class PluginSlotFactory;
class SineSynthFactory;
class InstrumentSlotFactory;
class AudioInputFactory;
class MonitorSwitchFactory;
class AudioSequencerFactory;
class MidiSequencerFactory;
class AudioTrackFactory;
class InstrumentTrackFactory;
} // namespace DSP

namespace Layer3 {
class IStreamingBuffer;
class IButlerThread;
} // namespace Layer3

namespace Layer1 {
class IFileSystem;
}

namespace Layer2 {
class IMutationBridge;
}

namespace bridge {

class TrackPipelineBuilderBase : public composition::ITrackPipelineBuilder {
public:
  TrackPipelineBuilderBase(DSP::LatencyFactory *latencyF,
                           DSP::ChannelStripFactory *csF,
                           DSP::PannerFactory *panF, DSP::SendFactory *sendF,
                           DSP::PluginSlotFactory *slotF,
                           Layer2::IMutationBridge *mutationBridge = nullptr,
                           NodeID masterBusNode = NodeID::invalid(),
                           DSP::AudioTrackFactory *audioTrackF = nullptr,
                           DSP::InstrumentTrackFactory *instrumentTrackF = nullptr,
                           DSP::InstrumentSlotFactory *instrumentSlotF = nullptr,
                           DSP::AudioInputFactory *audioInputF = nullptr);

  ~TrackPipelineBuilderBase() override = default;

  void destroyPipeline(const composition::TrackPipelineDescriptor &pipeline,
                       IDSPKernel *kernel) override;

protected:
  DSP::LatencyFactory *latencyF_;
  DSP::ChannelStripFactory *csF_;
  DSP::PannerFactory *panF_;
  DSP::SendFactory *sendF_;
  DSP::PluginSlotFactory *slotF_;
  Layer2::IMutationBridge *mutationBridge_;
  NodeID masterBusNode_;
  DSP::AudioTrackFactory *audioTrackF_ = nullptr;
  DSP::InstrumentTrackFactory *instrumentTrackF_ = nullptr;
  DSP::InstrumentSlotFactory *instrumentSlotF_ = nullptr;
  DSP::AudioInputFactory *audioInputF_ = nullptr;

  // 16-Bit Node Index Packing helper connect
  void pushConnectMutation(uint32_t srcType, NodeID srcId, uint32_t dstType,
                           NodeID dstId);

  void pushConnectMutation(uint32_t srcType, NodeID srcId, uint32_t srcPort,
                           uint32_t dstType, NodeID dstId, uint32_t dstPort);

  void pushAddNodeMutation(uint32_t type, NodeID id);
  void pushRemoveNodeMutation(uint32_t type, NodeID id);
};

class AudioTrackPipelineBuilder : public TrackPipelineBuilderBase {
public:
  AudioTrackPipelineBuilder(
      DSP::AudioSequencerFactory *audioSequencerF,
      DSP::LatencyFactory *latencyF, DSP::ChannelStripFactory *csF,
      DSP::PannerFactory *panF, DSP::SendFactory *sendF,
      DSP::PluginSlotFactory *slotF, Layer1::IFileSystem *fs,
      Layer3::IButlerThread *butler = nullptr,
      Layer2::IMutationBridge *mutationBridge = nullptr,
      NodeID masterBusNode = NodeID::invalid(),
      DSP::AudioInputFactory *audioInputF = nullptr,
      DSP::MonitorSwitchFactory *monitorSwitchF = nullptr,
      DSP::AudioTrackFactory *audioTrackF = nullptr);

  composition::TrackPipelineDescriptor
  buildPipeline(const composition::TrackCreateInfo &info,
                IDSPKernel *kernel) override;

  void destroyPipeline(const composition::TrackPipelineDescriptor &pipeline,
                       IDSPKernel *kernel) override;

  const std::vector<std::shared_ptr<Layer3::IStreamingBuffer>> &
  getActiveBuffers() const {
    return buffers_;
  }

private:
  DSP::AudioSequencerFactory *audioSequencerF_;
  DSP::AudioTrackFactory *audioTrackF_;
  Layer1::IFileSystem *fs_;
  Layer3::IButlerThread *butler_;
  DSP::AudioInputFactory *audioInputF_;
  DSP::MonitorSwitchFactory *monitorSwitchF_;
  std::vector<std::shared_ptr<Layer3::IStreamingBuffer>> buffers_;
};

class InstrumentTrackPipelineBuilder : public TrackPipelineBuilderBase {
public:
  InstrumentTrackPipelineBuilder(
      DSP::MidiSequencerFactory *midiSequencerF,
      DSP::InstrumentSlotFactory *instrumentSlotF,
      DSP::SineSynthFactory *synthF, DSP::LatencyFactory *latencyF,
      DSP::ChannelStripFactory *csF, DSP::PannerFactory *panF,
      DSP::SendFactory *sendF, DSP::PluginSlotFactory *slotF,
      Layer2::IMutationBridge *mutationBridge = nullptr,
      NodeID masterBusNode = NodeID::invalid(),
      DSP::InstrumentTrackFactory *instrumentTrackF = nullptr);

  composition::TrackPipelineDescriptor
  buildPipeline(const composition::TrackCreateInfo &info,
                IDSPKernel *kernel) override;

  void destroyPipeline(const composition::TrackPipelineDescriptor &pipeline,
                       IDSPKernel *kernel) override;

private:
  DSP::MidiSequencerFactory *midiSequencerF_;
  DSP::InstrumentSlotFactory *instrumentSlotF_;
  DSP::SineSynthFactory *synthF_;
  DSP::InstrumentTrackFactory *instrumentTrackF_;
};

/**
 * @brief Monolithic router/orchestrator builder to avoid changing session-level
 * code. Delegating track pipeline builder that invokes sub-builders depending
 * on track type.
 */
class TrackPipelineBuilder : public composition::ITrackPipelineBuilder {
public:
  TrackPipelineBuilder(DSP::AudioSequencerFactory *audioSequencerF,
                       DSP::MidiSequencerFactory *midiSequencerF,
                       DSP::LatencyFactory *latencyF,
                       DSP::ChannelStripFactory *csF, DSP::PannerFactory *panF,
                       DSP::SendFactory *sendF, DSP::PluginSlotFactory *slotF,
                       Layer1::IFileSystem *fs,
                       Layer3::IButlerThread *butler = nullptr,
                       Layer2::IMutationBridge *mutationBridge = nullptr,
                       NodeID masterBusNode = NodeID::invalid(),
                       DSP::SineSynthFactory *sineSynthF = nullptr,
                       DSP::InstrumentSlotFactory *instrumentSlotF = nullptr,
                       DSP::AudioInputFactory *audioInputF = nullptr,
                       DSP::MonitorSwitchFactory *monitorSwitchF = nullptr,
                       DSP::AudioTrackFactory *audioTrackF = nullptr,
                       DSP::InstrumentTrackFactory *instrumentTrackF = nullptr);

  composition::TrackPipelineDescriptor
  buildPipeline(const composition::TrackCreateInfo &info,
                IDSPKernel *kernel) override;

  void destroyPipeline(const composition::TrackPipelineDescriptor &pipeline,
                       IDSPKernel *kernel) override;

  const std::vector<std::shared_ptr<Layer3::IStreamingBuffer>> &
  getActiveBuffers() const {
    return audioBuilder_.getActiveBuffers();
  }

private:
  AudioTrackPipelineBuilder audioBuilder_;
  InstrumentTrackPipelineBuilder instrumentBuilder_;
  DSP::AudioSequencerFactory *audioSequencerF_;
  DSP::MidiSequencerFactory *midiSequencerF_;
  DSP::LatencyFactory *latencyF_;
  DSP::ChannelStripFactory *csF_;
  DSP::PannerFactory *panF_;
  DSP::SendFactory *sendF_;
  DSP::PluginSlotFactory *slotF_;
  Layer2::IMutationBridge *mutationBridge_;
  NodeID masterBusNode_;
  DSP::AudioInputFactory *audioInputF_;
  DSP::MonitorSwitchFactory *monitorSwitchF_;
  DSP::AudioTrackFactory *audioTrackF_;
  DSP::InstrumentTrackFactory *instrumentTrackF_;
};

} // namespace bridge
