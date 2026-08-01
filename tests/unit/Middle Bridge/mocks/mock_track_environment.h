#pragma once
#include "Middle Bridge/tracks/track_controller.h"
#include "Core infrastructure/bridges/imutation_bridge.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "DSP nodes/channelstrip/channel_strip_node.h"
#include "DSP nodes/panner/panner_node.h"
#include "DSP nodes/tracks/audio_track_node.h"
#include "DSP nodes/tracks/instrument_track_node.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "musical_composition/project_session/iproject_session.h"
#include "musical_composition/interfaces/iregion_metadata_manager.h"
#include "musical_composition/automation/iautomation_lane_manager.h"
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>

namespace Layer2 { class IEventQueue; class ITempoService; }
namespace composition { class IArrangementManager; class ICommandHistory; class IArrangerTrack; class IChordTrack; class IAudioRegionSourceManager; class IClipboard; class ICompingEngine; class IPlaylist; class IMIDISequencer; }

class MockTrackManager : public composition::ITrackManager {
public:
    struct MockTrack {
        composition::TrackCreateInfo info;
        uint32_t indexPosition = 0;
        TrackID parentFolderId = {0, 0};
        composition::TrackPipelineDescriptor pipeline;
    };
    
    std::unordered_map<uint32_t, MockTrack> tracks;
    std::unordered_map<TrackID, bool> lockedTracks;

    uint32_t nextId = 1;

    TrackID createTrack(const composition::TrackCreateInfo& info) override {
        TrackID id;
        id.id = nextId++;
        id.generation = 1;
        
        MockTrack t;
        t.info = info;
        t.indexPosition = static_cast<uint32_t>(tracks.size());
        
        t.pipeline.trackNode = DSP::AudioTrackFactory::getRegistry().allocate().value_or(NodeID::invalid());
        if (t.pipeline.trackNode.isValid()) {
            if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(t.pipeline.trackNode)) {
                trk->channelStrip.reset(44100.0f);
            }
        }
        
        tracks[id.id] = t;
        return id;
    }
    
    void deleteTrack(TrackID id) override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            if (it->second.pipeline.trackNode.isValid()) {
                DSP::AudioTrackFactory::getRegistry().deallocate(it->second.pipeline.trackNode);
            }
            tracks.erase(it);
        }
    }

    void renameTrack(TrackID id, uint32_t newNameId) override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            it->second.info.nameId = newNameId;
        }
    }

    void setTrackComments(TrackID id, uint32_t commentsId) override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            it->second.info.commentsId = commentsId;
        }
    }

    void setTrackOutputRouting(TrackID id, TrackID destinationTrackId) override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            it->second.info.outputTargetTrackId = destinationTrackId;
        }
    }

    void moveTrack(TrackID id, uint32_t newIndexPosition, TrackID newParentFolderId) override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            it->second.indexPosition = newIndexPosition;
            it->second.parentFolderId = newParentFolderId;
        }
    }

    void setTrackColor(TrackID id, uint32_t newColorARGB) override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            it->second.info.colorARGB = newColorARGB;
        }
    }

    void setTrackRecordArmed(TrackID id, bool armed) override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            it->second.info.isRecordArmed = armed;
        }
    }

    void setTrackInputMonitoring(TrackID id, bool enabled) override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            it->second.info.isInputMonitoring = enabled;
        }
    }

    void setTrackType(TrackID id, composition::TrackType type) override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            it->second.info.type = type;
        }
    }

    void setTrackTakesExpanded(TrackID, bool) override {}

    void setTrackLocked(TrackID id, bool locked) override {
        lockedTracks[id] = locked;
    }
    bool isTrackLocked(TrackID id) const override {
        auto it = lockedTracks.find(id);
        if (it != lockedTracks.end()) return it->second;
        return false;
    }


    composition::IPlaylist* getPlaylist(TrackID) override { return nullptr; }
    composition::IMIDISequencer* getMIDISequencer(TrackID) override { return nullptr; }

    composition::IAutomationLaneManager* getAutomationManager(TrackID) override { return nullptr; }
    
    composition::TrackPipelineDescriptor getPipelineDescriptor(TrackID id) const override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            return it->second.pipeline;
        }
        return composition::TrackPipelineDescriptor{};
    }

    NodeID getTrackOutputNode(TrackID) const override { return NodeID::invalid(); }

    std::vector<TrackID> getAllTrackIDs() const override {
        std::vector<TrackID> ids;
        ids.reserve(tracks.size());
        for (auto const& [key, _] : tracks) {
            TrackID id;
            id.id = key;
            id.generation = 1;
            ids.push_back(id);
        }
        return ids;
    }

    bool getTrackInfo(TrackID id, composition::TrackCreateInfo& outInfo) const override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            outInfo = it->second.info;
            return true;
        }
        return false;
    }

    uint32_t getTrackIndexPosition(TrackID id) const override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            return it->second.indexPosition;
        }
        return 0;
    }

    TrackID getTrackParentFolderId(TrackID id) const override {
        auto it = tracks.find(id.id);
        if (it != tracks.end()) {
            return it->second.parentFolderId;
        }
        return TrackID{0, 0};
    }

    std::atomic<uint64_t>* getRecordingStartSample(TrackID) override { return nullptr; }

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

    Layer2::IMutationBridge* mutationBridge = nullptr;

    // Mixer Operations
    void setTrackFaderGain(TrackID id, float gainLinear) override {
        auto desc = getPipelineDescriptor(id);
        if (desc.trackNode.isValid()) {
            if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
                trk->channelStrip.targetGain.store(gainLinear, std::memory_order_release);
                trk->channelStrip.gainSmoother.setTarget(gainLinear);
            } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
                trkInst->channelStrip.targetGain.store(gainLinear, std::memory_order_release);
                trkInst->channelStrip.gainSmoother.setTarget(gainLinear);
            } else if (auto* cs = DSP::ChannelStripFactory::getRegistry().get(desc.trackNode)) {
                cs->targetGain.store(gainLinear, std::memory_order_release);
                cs->gainSmoother.setTarget(gainLinear);
            }
            if (mutationBridge) {
                SystemMutation mut{};
                mut.type = 30; // PARAMETER_SET
                mut.targetId = desc.trackNode;
                mut.priority = 128;
                mut.payload[0] = static_cast<uint32_t>(TrackMacroParameter::Volume);
                std::memcpy(&mut.payload[1], &gainLinear, sizeof(float));
                mutationBridge->pushMutation(mut);
            }
        }
    }

    void setTrackPan(TrackID id, float panPosition) override {
        auto desc = getPipelineDescriptor(id);
        if (desc.trackNode.isValid()) {
            if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
                trk->channelStrip.targetPan.store(panPosition, std::memory_order_release);
                trk->channelStrip.panSmoother.setTarget(panPosition);
            } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
                trkInst->channelStrip.targetPan.store(panPosition, std::memory_order_release);
                trkInst->channelStrip.panSmoother.setTarget(panPosition);
            } else if (auto* cs = DSP::ChannelStripFactory::getRegistry().get(desc.trackNode)) {
                cs->targetPan.store(panPosition, std::memory_order_release);
                cs->panSmoother.setTarget(panPosition);
            }
            if (mutationBridge) {
                SystemMutation mut{};
                mut.type = 30;
                mut.targetId = desc.trackNode;
                mut.priority = 128;
                mut.payload[0] = static_cast<uint32_t>(TrackMacroParameter::Pan);
                std::memcpy(&mut.payload[1], &panPosition, sizeof(float));
                mutationBridge->pushMutation(mut);
            }
        }
    }

    void setTrackMute(TrackID id, bool mute) override {
        auto desc = getPipelineDescriptor(id);
        if (desc.trackNode.isValid()) {
            if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
                trk->channelStrip.mute = mute;
            } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
                trkInst->channelStrip.mute = mute;
            } else if (auto* cs = DSP::ChannelStripFactory::getRegistry().get(desc.trackNode)) {
                cs->mute = mute;
            }
            if (mutationBridge) {
                SystemMutation mut{};
                mut.type = 30;
                mut.targetId = desc.trackNode;
                mut.priority = 128;
                mut.payload[0] = static_cast<uint32_t>(TrackMacroParameter::Mute);
                float val = mute ? 1.0f : 0.0f;
                std::memcpy(&mut.payload[1], &val, sizeof(float));
                mutationBridge->pushMutation(mut);
            }
        }
    }

    void setTrackSolo(TrackID id, bool solo) override {
        auto desc = getPipelineDescriptor(id);
        if (desc.trackNode.isValid()) {
            if (auto* trk = DSP::AudioTrackFactory::getRegistry().get(desc.trackNode)) {
                trk->channelStrip.solo = solo;
            } else if (auto* trkInst = DSP::InstrumentTrackFactory::getRegistry().get(desc.trackNode)) {
                trkInst->channelStrip.solo = solo;
            } else if (auto* cs = DSP::ChannelStripFactory::getRegistry().get(desc.trackNode)) {
                cs->solo = solo;
            }
            if (mutationBridge) {
                SystemMutation mut{};
                mut.type = 30;
                mut.targetId = desc.trackNode;
                mut.priority = 128;
                mut.payload[0] = static_cast<uint32_t>(TrackMacroParameter::Solo);
                float val = solo ? 1.0f : 0.0f;
                std::memcpy(&mut.payload[1], &val, sizeof(float));
                mutationBridge->pushMutation(mut);
            }
        }
    }

    // Mixer Queries
    float getTrackFaderGain(TrackID) const override { return 1.0f; }
    float getTrackPan(TrackID) const override { return 0.5f; }
    bool detectFeedbackCycle(TrackID sourceTrackId, TrackID targetTrackId) const override {
        for (const auto& [key, routing] : sidechainRoutings) {
            TrackID tId = TrackID::fromRaw(key >> 8);
            if (tId == sourceTrackId && routing.sourceTrackId == targetTrackId) {
                return true;
            }
        }
        return false;
    }

    struct SidechainRouting {
        TrackID sourceTrackId;
        float gainLinear = 1.0f;
    };
    std::unordered_map<uint64_t, SidechainRouting> sidechainRoutings;

    bool setTrackSidechainRouting(TrackID targetTrackId, uint32_t slotIndex, TrackID sourceTrackId, float gainLinear) override {
        uint64_t key = (targetTrackId.toRaw() << 8) | (slotIndex & 0xFF);
        sidechainRoutings[key] = {sourceTrackId, gainLinear};
        return true;
    }

    void clearTrackSidechainRouting(TrackID targetTrackId, uint32_t slotIndex) override {
        uint64_t key = (targetTrackId.toRaw() << 8) | (slotIndex & 0xFF);
        sidechainRoutings.erase(key);
    }

    bool getTrackSidechainRouting(TrackID targetTrackId, uint32_t slotIndex, TrackID& outSourceTrackId, float& outGainLinear) const override {
        uint64_t key = (targetTrackId.toRaw() << 8) | (slotIndex & 0xFF);
        auto it = sidechainRoutings.find(key);
        if (it != sidechainRoutings.end()) {
            outSourceTrackId = it->second.sourceTrackId;
            outGainLinear = it->second.gainLinear;
            return true;
        }
        return false;
    }
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

    // Callback Registration
    composition::MixerRoutingCallback mixerRoutingCallback;
    void registerMixerRoutingCallback(composition::MixerRoutingCallback cb) override {
        mixerRoutingCallback = cb;
    }
};

class MockMeteringProvider : public bridge::IMeteringProvider {
public:
    std::unordered_map<uint64_t, bridge::MeterLevel> levels;
    std::unordered_map<uint64_t, NodeID> mappings;

    bridge::MeterLevel getTrackLevels(TrackID id) override {
        auto it = levels.find(id.toRaw());
        if (it != levels.end()) {
            return it->second;
        }
        return bridge::MeterLevel{};
    }

    void resetTrackClip(TrackID /*id*/) override {}
    bridge::MeterLevel getMasterLevels() override { return bridge::MeterLevel{}; }
    void resetMasterClip() override {}

    void registerTrackNodeMapping(TrackID trackId, NodeID nodeId) override {
        mappings[trackId.toRaw()] = nodeId;
    }

    void unregisterTrackNodeMapping(TrackID trackId) override {
        mappings.erase(trackId.toRaw());
    }

    void updateMeters(double) override {}
    void getSpectrumData(NodeID, float*, uint32_t) override {}
};

class MockRegionMetadataManager : public composition::IRegionMetadataManager {
public:
    void getRegionMetadata(RegionID, composition::RegionMetadata&) const override {}
    bool hasRegionMetadata(RegionID) const override { return false; }
    void setRegionMetadata(RegionID, const composition::RegionMetadata&, bool) override {}
    void removeRegionMetadata(RegionID, bool) override {}
    void clear() override {}
};

class MockProjectSession : public composition::IProjectSession {
public:
    explicit MockProjectSession(composition::ITrackManager* tm) : trackManager(tm) {}

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

    bool saveToFile(const std::string&, Layer2::IStringRegistry* = nullptr) override { return true; }
    bool loadFromFile(const std::string&, Layer2::IStringRegistry* = nullptr, std::vector<composition::MissingPluginReport>* = nullptr) override { return true; }
    bool saveToJsonFile(const std::string&, Layer2::IStringRegistry* = nullptr) override { return true; }
    bool loadFromJsonFile(const std::string&, Layer2::IStringRegistry* = nullptr, std::vector<composition::MissingPluginReport>* = nullptr) override { return true; }

    composition::ProjectMetadata metadata;
    composition::MixStatistics mixStats;
    composition::ITrackManager* trackManager;
    MockRegionMetadataManager metaManager;
};

class MockSessionManager : public bridge::ISessionManager {
public:
    void registerChangeListener(bridge::ISessionChangeListener* listener) override {
        listeners.push_back(listener);
    }
    void unregisterChangeListener(bridge::ISessionChangeListener* listener) override {
        listeners.erase(std::remove(listeners.begin(), listeners.end(), listener), listeners.end());
    }
    composition::IProjectSession* getActiveSession() const override {
        return activeSession;
    }
    void setActiveSession(std::unique_ptr<composition::IProjectSession> session) override {
        for (auto* l : listeners) l->onSessionChanging();
        activeSession = session.get();
        ownedSession = std::move(session);
        for (auto* l : listeners) l->onSessionChanged(activeSession);
    }
    void closeActiveSession() override {
        for (auto* l : listeners) l->onSessionChanging();
        activeSession = nullptr;
        ownedSession.reset();
        for (auto* l : listeners) l->onSessionChanged(nullptr);
    }
    void triggerSessionRefresh() override {}
    void onTempoMapChanged(Layer2::ITempoService*) override {}
    Layer2::IStringRegistry* getStringRegistry() const override { return nullptr; }


    std::vector<bridge::ISessionChangeListener*> listeners;
    composition::IProjectSession* activeSession = nullptr;
    std::unique_ptr<composition::IProjectSession> ownedSession;
};
