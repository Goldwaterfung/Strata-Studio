#include "tracks/track_pipeline_builder.h"
#include "Core audio engine/plugin/iplugin.h"
#include "Core audio engine/scheduler/idsp_kernel.h"
#include "Core audio engine/streaming/ibutler_thread.h"
#include "Core infrastructure/bridges/imutation_bridge.h"
#include "DSP nodes/audio_input/audio_input_node.h"
#include "DSP nodes/channelstrip/channel_strip_node.h"
#include "DSP nodes/latency/latency_node.h"
#include "DSP nodes/monitor_switch/monitor_switch_node.h"
#include "DSP nodes/panner/panner_node.h"
#include "DSP nodes/plugins/insert_plugin_node.h"
#include "DSP nodes/plugins/instrument_slot_node.h"
#include "DSP nodes/plugins/plugin_slot_node.h"
#include "DSP nodes/sends/send_node.h"
#include "DSP nodes/sequencer/audio_sequencer_node.h"
#include "DSP nodes/sequencer/midi_sequencer_node.h"
#include "DSP nodes/tracks/audio_track_node.h"
#include "DSP nodes/tracks/instrument_track_node.h"
#include "Hardware/OS abstraction/filesystem/ifile_system.h"
#include <algorithm>
#include <cstdio>
#include <iostream>

namespace bridge {

// ============================================================================
// TrackPipelineBuilderBase Implementation
// ============================================================================

TrackPipelineBuilderBase::TrackPipelineBuilderBase(
    DSP::LatencyFactory *latencyF, DSP::ChannelStripFactory *csF,
    DSP::PannerFactory *panF, DSP::SendFactory *sendF,
    DSP::PluginSlotFactory *slotF, Layer2::IMutationBridge *mutationBridge,
    NodeID masterBusNode,
    DSP::AudioTrackFactory *audioTrackF,
    DSP::InstrumentTrackFactory *instrumentTrackF,
    DSP::InstrumentSlotFactory *instrumentSlotF,
    DSP::AudioInputFactory *audioInputF)
    : latencyF_(latencyF), csF_(csF), panF_(panF), sendF_(sendF), slotF_(slotF),
      mutationBridge_(mutationBridge), masterBusNode_(masterBusNode),
      audioTrackF_(audioTrackF), instrumentTrackF_(instrumentTrackF),
      instrumentSlotF_(instrumentSlotF), audioInputF_(audioInputF) {}

void TrackPipelineBuilderBase::pushConnectMutation(uint32_t srcType,
                                                   NodeID srcId,
                                                   uint32_t dstType,
                                                   NodeID dstId) {
  pushConnectMutation(srcType, srcId, 0, dstType, dstId, 0);
  pushConnectMutation(srcType, srcId, 1, dstType, dstId, 1);
}

void TrackPipelineBuilderBase::pushConnectMutation(
    uint32_t srcType, NodeID srcId, uint32_t srcPort, uint32_t dstType,
    NodeID dstId, uint32_t dstPort) {
  if (!srcId.isValid() || !dstId.isValid() || !mutationBridge_)
    return;

  SystemMutation connM{};
  connM.priority = 0;
  connM.type = Layer2::MutationType::NODE_CONNECT;

  // 16-Bit Node Index Packing Calculation
  connM.connection.sourceNodeIndex = (srcType << 16) | (srcId.id & 0xFFFF);
  connM.connection.sourcePort = srcPort;
  connM.connection.destNodeIndex = (dstType << 16) | (dstId.id & 0xFFFF);
  connM.connection.destPort = dstPort;
  connM.connection.gain = 1.0f;

  mutationBridge_->pushMutation(connM);
}

void TrackPipelineBuilderBase::pushAddNodeMutation(uint32_t type, NodeID id) {
  if (!id.isValid() || !mutationBridge_)
    return;

  SystemMutation addM{};
  addM.priority = 0;
  addM.type = Layer2::MutationType::NODE_ADD;
  addM.node.type = type;
  addM.node.id = id;

  mutationBridge_->pushMutation(addM);
}

void TrackPipelineBuilderBase::pushRemoveNodeMutation(uint32_t type, NodeID id) {
  if (!id.isValid() || !mutationBridge_)
    return;

  SystemMutation removeM{};
  removeM.priority = 0;
  removeM.type = Layer2::MutationType::NODE_REMOVE;
  removeM.node.type = type;
  removeM.node.id = id;

  mutationBridge_->pushMutation(removeM);
}

void TrackPipelineBuilderBase::destroyPipeline(
    const composition::TrackPipelineDescriptor &pipeline,
    IDSPKernel * /*kernel*/
) {
  if (pipeline.instrumentSlotNode.isValid()) {
    if (auto* slotNode = DSP::getInstrumentSlotState(pipeline.instrumentSlotNode)) {
      if (slotNode->pluginInstance) {
        delete static_cast<Layer3::IPlugin*>(slotNode->pluginInstance);
        slotNode->pluginInstance = nullptr;
      }
    }
    if (instrumentSlotF_) {
      pushRemoveNodeMutation(DSP::NODE_TYPE_INSTRUMENT_SLOT, pipeline.instrumentSlotNode);
      instrumentSlotF_->destroyNode(pipeline.instrumentSlotNode);
    }
  }
  if (pipeline.audioInputNode.isValid() && audioInputF_) {
    pushRemoveNodeMutation(DSP::NODE_TYPE_AUDIO_INPUT, pipeline.audioInputNode);
    audioInputF_->destroyNode(pipeline.audioInputNode);
  }
  if (pipeline.trackNode.isValid()) {
    if (audioTrackF_ && DSP::AudioTrackFactory::getRegistry().get(pipeline.trackNode)) {
      pushRemoveNodeMutation(DSP::NODE_TYPE_AUDIO_TRACK, pipeline.trackNode);
      audioTrackF_->destroyNode(pipeline.trackNode);
    } else if (instrumentTrackF_ && DSP::InstrumentTrackFactory::getRegistry().get(pipeline.trackNode)) {
      pushRemoveNodeMutation(DSP::NODE_TYPE_INSTRUMENT_TRACK, pipeline.trackNode);
      instrumentTrackF_->destroyNode(pipeline.trackNode);
    }
  }
}

// ============================================================================
// AudioTrackPipelineBuilder Implementation
// ============================================================================

AudioTrackPipelineBuilder::AudioTrackPipelineBuilder(
    DSP::AudioSequencerFactory *audioSequencerF, DSP::LatencyFactory *latencyF,
    DSP::ChannelStripFactory *csF, DSP::PannerFactory *panF,
    DSP::SendFactory *sendF, DSP::PluginSlotFactory *slotF,
    Layer1::IFileSystem *fs, Layer3::IButlerThread *butler,
    Layer2::IMutationBridge *mutationBridge, NodeID masterBusNode,
    DSP::AudioInputFactory *audioInputF,
    DSP::MonitorSwitchFactory *monitorSwitchF,
    DSP::AudioTrackFactory *audioTrackF)
    : TrackPipelineBuilderBase(latencyF, csF, panF, sendF, slotF,
                               mutationBridge, masterBusNode, audioTrackF, nullptr, nullptr, audioInputF),
      audioSequencerF_(audioSequencerF), audioTrackF_(audioTrackF), fs_(fs), butler_(butler),
      audioInputF_(audioInputF), monitorSwitchF_(monitorSwitchF) {}

composition::TrackPipelineDescriptor AudioTrackPipelineBuilder::buildPipeline(
    const composition::TrackCreateInfo &info, IDSPKernel * /*kernel*/
) {
  DSP::AudioSequencerFactory::setFileSystem(fs_);

  // 1. Create AudioSequencer Source Node
  NodeID samplerId =
      audioSequencerF_ ? audioSequencerF_->createNode() : NodeID::invalid();
  if (samplerId.isValid() && audioSequencerF_) {
    if (auto *s = audioSequencerF_->getRegistry().get(samplerId)) {
      s->trackId = info.trackId;
      s->targetGain = 1.0f;

      uint32_t channels =
          info.audioChannelCount > 0 ? info.audioChannelCount : 2;
      for (uint32_t i = 0; i < MAX_BUFFERS_PER_TRACK; ++i) {
        auto buffer = std::shared_ptr<Layer3::IStreamingBuffer>(
            Layer3::IStreamingBuffer::create(channels, 44100));
        if (buffer) {
          buffer->setBufferSize(44100 * 2);
          buffer->setReadAheadSize(22050); // 0.5s read-ahead
          s->buffers[i] = buffer.get();

          if (butler_) {
            butler_->registerBuffer(buffer.get());
            butler_->registerBufferForTrack(info.trackId.id, buffer.get());
          }
          buffers_.push_back(buffer);
        }
      }
    }
  }

  // 2. Create Hardware AudioInput Capture Node
  NodeID audioInputId =
      audioInputF_ ? audioInputF_->createNode() : NodeID::invalid();
  if (audioInputId.isValid() && audioInputF_) {
    if (auto *s = audioInputF_->getRegistry().get(audioInputId)) {
      uint8_t hwCh = static_cast<uint8_t>(info.inputSourceIndex);
      uint8_t numCh = static_cast<uint8_t>(
          info.audioChannelCount > 0 ? info.audioChannelCount : 2);
      s->buffers[0].hardwareChannelIndex = hwCh;
      s->buffers[0].numChannels = numCh;
      s->buffers[0].actualStartSample = info.recordingStartSample;
      s->buffers[1].hardwareChannelIndex = hwCh;
      s->buffers[1].numChannels = numCh;
      s->buffers[1].actualStartSample = info.recordingStartSample;
    }
  }

  // 3. Create Monolithic AudioTrack Node
  NodeID trackNodeId = audioTrackF_ ? audioTrackF_->createNode() : NodeID::invalid();

  // 4. Emit exact mutations to global DAG
  pushAddNodeMutation(DSP::NODE_TYPE_AUDIO_SEQUENCER, samplerId);
  pushAddNodeMutation(DSP::NODE_TYPE_AUDIO_INPUT, audioInputId);
  pushAddNodeMutation(DSP::NODE_TYPE_AUDIO_TRACK, trackNodeId);

  // Connect AudioInput (Outputs 0..1) -> AudioTrackNode (Inputs 0..1: TRACK_INPUT_HARDWARE_PORT_BASE)
  pushConnectMutation(DSP::NODE_TYPE_AUDIO_INPUT, audioInputId, 0,
                      DSP::NODE_TYPE_AUDIO_TRACK, trackNodeId, TRACK_INPUT_HARDWARE_PORT_BASE + 0);
  pushConnectMutation(DSP::NODE_TYPE_AUDIO_INPUT, audioInputId, 1,
                      DSP::NODE_TYPE_AUDIO_TRACK, trackNodeId, TRACK_INPUT_HARDWARE_PORT_BASE + 1);

  // Connect AudioSequencer (Outputs 0..1) -> AudioTrackNode (Inputs 2..3: TRACK_INPUT_PLAYBACK_PORT_BASE)
  pushConnectMutation(DSP::NODE_TYPE_AUDIO_SEQUENCER, samplerId, 0,
                      DSP::NODE_TYPE_AUDIO_TRACK, trackNodeId, TRACK_INPUT_PLAYBACK_PORT_BASE + 0);
  pushConnectMutation(DSP::NODE_TYPE_AUDIO_SEQUENCER, samplerId, 1,
                      DSP::NODE_TYPE_AUDIO_TRACK, trackNodeId, TRACK_INPUT_PLAYBACK_PORT_BASE + 1);

  // Connect AudioTrackNode (Main Out 0..1) -> Master Bus Node (Inputs 0..1)
  pushConnectMutation(DSP::NODE_TYPE_AUDIO_TRACK, trackNodeId, 0,
                      DSP::NODE_TYPE_BUS, masterBusNode_, 0);
  pushConnectMutation(DSP::NODE_TYPE_AUDIO_TRACK, trackNodeId, 1,
                      DSP::NODE_TYPE_BUS, masterBusNode_, 1);

  composition::TrackPipelineDescriptor desc{};
  desc.sourceNode = samplerId;
  desc.audioInputNode = audioInputId;
  desc.trackNode = trackNodeId;
  return desc;
}

void AudioTrackPipelineBuilder::destroyPipeline(
    const composition::TrackPipelineDescriptor &pipeline, IDSPKernel *kernel) {
  if (pipeline.sourceNode.isValid() && audioSequencerF_) {
    if (auto *s = audioSequencerF_->getRegistry().get(pipeline.sourceNode)) {
      for (uint32_t i = 0; i < MAX_BUFFERS_PER_TRACK; ++i) {
        if (s->buffers[i]) {
          if (butler_) {
            butler_->unregisterBuffer(s->buffers[i]);
          }

          auto it = std::remove_if(
              buffers_.begin(), buffers_.end(),
              [s, i](const std::shared_ptr<Layer3::IStreamingBuffer> &b) {
                return b.get() == s->buffers[i];
              });
          if (it != buffers_.end()) {
            buffers_.erase(it, buffers_.end());
          }
        }
      }
      if (butler_) {
        butler_->unregisterBufferForTrack(s->trackId.id);
      }
    }
    pushRemoveNodeMutation(DSP::NODE_TYPE_AUDIO_SEQUENCER, pipeline.sourceNode);
    audioSequencerF_->destroyNode(pipeline.sourceNode);
  }

  if (pipeline.audioInputNode.isValid() && audioInputF_) {
    pushRemoveNodeMutation(DSP::NODE_TYPE_AUDIO_INPUT, pipeline.audioInputNode);
    audioInputF_->destroyNode(pipeline.audioInputNode);
  }

  if (pipeline.trackNode.isValid() && audioTrackF_) {
    pushRemoveNodeMutation(DSP::NODE_TYPE_AUDIO_TRACK, pipeline.trackNode);
    audioTrackF_->destroyNode(pipeline.trackNode);
  }

  TrackPipelineBuilderBase::destroyPipeline(pipeline, kernel);
}

// ============================================================================
// InstrumentTrackPipelineBuilder Implementation
// ============================================================================

InstrumentTrackPipelineBuilder::InstrumentTrackPipelineBuilder(
    DSP::MidiSequencerFactory *midiSequencerF,
    DSP::InstrumentSlotFactory *instrumentSlotF, DSP::SineSynthFactory *synthF,
    DSP::LatencyFactory *latencyF, DSP::ChannelStripFactory *csF,
    DSP::PannerFactory *panF, DSP::SendFactory *sendF,
    DSP::PluginSlotFactory *slotF, Layer2::IMutationBridge *mutationBridge,
    NodeID masterBusNode,
    DSP::InstrumentTrackFactory *instrumentTrackF)
    : TrackPipelineBuilderBase(latencyF, csF, panF, sendF, slotF,
                               mutationBridge, masterBusNode, nullptr, instrumentTrackF, instrumentSlotF, nullptr),
      midiSequencerF_(midiSequencerF), instrumentSlotF_(instrumentSlotF),
      synthF_(synthF), instrumentTrackF_(instrumentTrackF) {}

composition::TrackPipelineDescriptor
InstrumentTrackPipelineBuilder::buildPipeline(
    const composition::TrackCreateInfo &info, IDSPKernel * /*kernel*/
) {
  // 1. MidiSequencer Node
  NodeID sequencerId =
      midiSequencerF_ ? midiSequencerF_->createNode() : NodeID::invalid();
  if (sequencerId.isValid() && midiSequencerF_) {
    if (auto *s = midiSequencerF_->getRegistry().get(sequencerId)) {
      s->trackId = info.trackId;
    }
  }

  // 2. Monolithic InstrumentTrack Node
  NodeID trackNodeId = instrumentTrackF_ ? instrumentTrackF_->createNode() : NodeID::invalid();

  pushAddNodeMutation(DSP::NODE_TYPE_MIDI_SEQUENCER, sequencerId);
  pushAddNodeMutation(DSP::NODE_TYPE_INSTRUMENT_TRACK, trackNodeId);

  if (sequencerId.isValid() && trackNodeId.isValid()) {
    pushConnectMutation(DSP::NODE_TYPE_MIDI_SEQUENCER, sequencerId,
                        DSP::NODE_TYPE_INSTRUMENT_TRACK, trackNodeId);
  }
  if (trackNodeId.isValid()) {
    pushConnectMutation(DSP::NODE_TYPE_INSTRUMENT_TRACK, trackNodeId, 0,
                        DSP::NODE_TYPE_BUS, masterBusNode_, 0);
    pushConnectMutation(DSP::NODE_TYPE_INSTRUMENT_TRACK, trackNodeId, 1,
                        DSP::NODE_TYPE_BUS, masterBusNode_, 1);
  }

  composition::TrackPipelineDescriptor desc{};
  desc.sourceNode = sequencerId;
  desc.trackNode = trackNodeId;
  desc.instrumentSlotNode = trackNodeId;
  desc.latencySamples = 0;

  return desc;
}

void InstrumentTrackPipelineBuilder::destroyPipeline(
    const composition::TrackPipelineDescriptor &pipeline, IDSPKernel *kernel) {
  if (pipeline.sourceNode.isValid() && midiSequencerF_) {
    pushRemoveNodeMutation(DSP::NODE_TYPE_MIDI_SEQUENCER, pipeline.sourceNode);
    midiSequencerF_->destroyNode(pipeline.sourceNode);
  }
  if (pipeline.trackNode.isValid() && instrumentTrackF_) {
    pushRemoveNodeMutation(DSP::NODE_TYPE_INSTRUMENT_TRACK, pipeline.trackNode);
    instrumentTrackF_->destroyNode(pipeline.trackNode);
  }
  TrackPipelineBuilderBase::destroyPipeline(pipeline, kernel);
}

// ============================================================================
// Master Orchestrator TrackPipelineBuilder Implementation
// ============================================================================

TrackPipelineBuilder::TrackPipelineBuilder(
    DSP::AudioSequencerFactory *audioSequencerF,
    DSP::MidiSequencerFactory *midiSequencerF, DSP::LatencyFactory *latencyF,
    DSP::ChannelStripFactory *csF, DSP::PannerFactory *panF,
    DSP::SendFactory *sendF, DSP::PluginSlotFactory *slotF,
    Layer1::IFileSystem *fs, Layer3::IButlerThread *butler,
    Layer2::IMutationBridge *mutationBridge, NodeID masterBusNode,
    DSP::SineSynthFactory *sineSynthF,
    DSP::InstrumentSlotFactory *instrumentSlotF,
    DSP::AudioInputFactory *audioInputF,
    DSP::MonitorSwitchFactory *monitorSwitchF,
    DSP::AudioTrackFactory *audioTrackF,
    DSP::InstrumentTrackFactory *instrumentTrackF)
    : audioBuilder_(audioSequencerF, latencyF, csF, panF, sendF, slotF, fs,
                    butler, mutationBridge, masterBusNode, audioInputF,
                    monitorSwitchF, audioTrackF),
      instrumentBuilder_(midiSequencerF, instrumentSlotF, sineSynthF, latencyF,
                         csF, panF, sendF, slotF, mutationBridge,
                         masterBusNode, instrumentTrackF),
      audioSequencerF_(audioSequencerF), midiSequencerF_(midiSequencerF),
      latencyF_(latencyF), csF_(csF), panF_(panF), sendF_(sendF), slotF_(slotF),
      mutationBridge_(mutationBridge), masterBusNode_(masterBusNode),
      audioInputF_(audioInputF), monitorSwitchF_(monitorSwitchF),
      audioTrackF_(audioTrackF), instrumentTrackF_(instrumentTrackF) {}

composition::TrackPipelineDescriptor
TrackPipelineBuilder::buildPipeline(const composition::TrackCreateInfo &info,
                                    IDSPKernel *kernel) {
  if (info.type == composition::TrackType::AUDIO) {
    return audioBuilder_.buildPipeline(info, kernel);
  } else if (info.type == composition::TrackType::MIDI ||
             info.type == composition::TrackType::INSTRUMENT) {
    return instrumentBuilder_.buildPipeline(info, kernel);
  }

  // AUX/MASTER/FOLDER tracks use monolithic AudioTrackNode macro-node
  NodeID trackNodeId = audioTrackF_ ? audioTrackF_->createNode() : NodeID::invalid();
  if (trackNodeId.isValid() && audioTrackF_) {
    if (auto *trk = audioTrackF_->getRegistry().get(trackNodeId)) {
      trk->channelStrip.reset(44100.0f);
      trk->channelStrip.targetGain = 1.0f;
      trk->channelStrip.targetPan = 0.5f;
    }
    if (mutationBridge_) {
      SystemMutation addM{};
      addM.priority = 0;
      addM.type = Layer2::MutationType::NODE_ADD;
      addM.node.type = DSP::NODE_TYPE_AUDIO_TRACK;
      addM.node.id = trackNodeId;
      mutationBridge_->pushMutation(addM);

      if (info.type != composition::TrackType::MASTER && masterBusNode_.isValid()) {
        for (uint32_t ch = 0; ch < 2; ++ch) {
          SystemMutation connM{};
          connM.priority = 0;
          connM.type = Layer2::MutationType::NODE_CONNECT;
          connM.connection.sourceNodeIndex = (DSP::NODE_TYPE_AUDIO_TRACK << 16) | (trackNodeId.id & 0xFFFF);
          connM.connection.sourcePort = ch;
          connM.connection.destNodeIndex = (DSP::NODE_TYPE_BUS << 16) | (masterBusNode_.id & 0xFFFF);
          connM.connection.destPort = ch;
          connM.connection.gain = 1.0f;
          mutationBridge_->pushMutation(connM);
        }
      }
    }
  }

  composition::TrackPipelineDescriptor desc{};
  desc.sourceNode = NodeID::invalid();
  desc.trackNode = trackNodeId;
  desc.latencySamples = 0;

  return desc;
}

void TrackPipelineBuilder::destroyPipeline(
    const composition::TrackPipelineDescriptor &pipeline, IDSPKernel *kernel) {
  if (pipeline.sourceNode.isValid()) {
    if (audioSequencerF_ &&
        audioSequencerF_->getRegistry().get(pipeline.sourceNode) != nullptr) {
      audioBuilder_.destroyPipeline(pipeline, kernel);
    } else if (midiSequencerF_ && midiSequencerF_->getRegistry().get(
                                      pipeline.sourceNode) != nullptr) {
      instrumentBuilder_.destroyPipeline(pipeline, kernel);
    } else {
      audioBuilder_.destroyPipeline(pipeline, kernel);
    }
  } else {
    // AUX/MASTER/FOLDER tracks (destroys preSend, channelStrip, panner, send,
    // inserts via base destroy)
    audioBuilder_.destroyPipeline(pipeline, kernel);
  }
}

} // namespace bridge
