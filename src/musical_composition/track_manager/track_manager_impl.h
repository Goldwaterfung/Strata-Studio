#pragma once
#include "itrack_manager.h"
#include "musical_composition/interfaces/itrack_pipeline_builder.h"
#include "musical_composition/playlist/iplaylist.h"
#include "musical_composition/midi_sequencer/imidi_sequencer.h"
#include "musical_composition/automation/iautomation_lane_manager.h"
#include "musical_composition/command_history/delta_primitives.h"
#include "musical_composition/command_history/icommand_history.h"
#include "musical_composition/interfaces/iarrangement_manager.h"
#include "mixer_routing_commands.h"
#include <unordered_map>
#include <memory>
#include <vector>
#include <atomic>
#include <string>

namespace Layer2 { class IMutationBridge; }
namespace Layer3 { class IPluginManager; }
namespace DSP { class LatencyFactory; }

namespace Layer2 { class IEventQueue; }

namespace composition {

class IAudioRegionSourceManager;

class TrackManagerImpl : public ITrackManager, public IMidiClipDataProvider, public IArrangementManager {
    friend struct ProjectSerializer;
    friend class ProjectStateBridge;
public:
    TrackManagerImpl(
        std::unique_ptr<ITrackPipelineBuilder> builder,
        ICommandHistory* history,
        IAudioRegionSourceManager* sourceManager,
        IDSPKernel* kernel,
        Layer2::IMutationBridge* mutationBridge,
        Layer3::IPluginManager* pluginManager,
        NodeID masterChannelStripNode,
        NodeID masterPluginSlotNode,
        NodeID masterLatencyNode,
        DSP::LatencyFactory* latencyFactory
    );
    ~TrackManagerImpl() override;

    TrackID createTrack(const TrackCreateInfo& info) override;
    void deleteTrack(TrackID id) override;

    void renameTrack(TrackID id, uint32_t newNameId) override;
    void setTrackComments(TrackID id, uint32_t commentsId) override;
    void setTrackOutputRouting(TrackID id, TrackID destinationTrackId) override;
    void moveTrack(TrackID id, uint32_t newIndexPosition, TrackID newParentFolderId) override;
    void setTrackColor(TrackID id, uint32_t newColorARGB) override;
    void setTrackRecordArmed(TrackID id, bool armed) override;
    void setTrackInputMonitoring(TrackID id, bool enabled) override;
    void setTrackType(TrackID id, TrackType type) override;
    void setTrackTakesExpanded(TrackID id, bool expanded) override;
    void setTrackLocked(TrackID id, bool locked) override;
    bool isTrackLocked(TrackID id) const override;


    // Mixer Operations
    void setTrackFaderGain(TrackID id, float gainLinear) override;
    void setTrackPan(TrackID id, float panPosition) override;
    void setTrackMute(TrackID id, bool mute) override;
    void setTrackSolo(TrackID id, bool solo) override;

    // Mixer Queries
    float getTrackFaderGain(TrackID id) const override;
    float getTrackPan(TrackID id) const override;
    bool getTrackMute(TrackID id) const override;
    bool getTrackSolo(TrackID id) const override;

    // Routing Operations
    void setTrackSendGain(TrackID id, bool isPreFader, uint32_t sendIndex, float gainLinear) override;
    void setTrackSendPan(TrackID id, bool isPreFader, uint32_t sendIndex, float panPosition) override;
    void setTrackSendEnabled(TrackID id, bool isPreFader, uint32_t sendIndex, bool enabled) override;
    void setTrackSendDestination(TrackID id, bool isPreFader, uint32_t sendIndex, NodeID destinationNodeId) override;
    void setTrackAudioInputChannel(TrackID id, uint32_t mappedPhysicalInputIndex, uint32_t numChannels) override;

    // Routing Queries
    float getTrackSendGain(TrackID id, bool isPreFader, uint32_t sendIndex) const override;
    float getTrackSendPan(TrackID id, bool isPreFader, uint32_t sendIndex) const override;
    bool getTrackSendEnabled(TrackID id, bool isPreFader, uint32_t sendIndex) const override;
    NodeID getTrackSendDestination(TrackID id, bool isPreFader, uint32_t sendIndex) const override;
    std::string getTrackSendDestinationName(TrackID id, bool isPreFader, uint32_t sendIndex) const override;

    // Plugin Operations
    void insertTrackPlugin(TrackID id, uint32_t slotIndex, uint32_t pluginId) override;
    void removeTrackPlugin(TrackID id, uint32_t slotIndex) override;
    void setTrackPluginBypassed(TrackID id, uint32_t slotIndex, bool bypassed) override;
    void insertTrackInstrument(TrackID id, uint32_t pluginId) override;
    void removeTrackInstrument(TrackID id) override;
    void setTrackInstrumentBypassed(TrackID id, bool bypassed) override;
    void completeTrackInstrumentInsertion(TrackID trackId, void* rawInstance, const struct PluginDescriptor& plugDesc) override;
    void completeTrackPluginInsertion(TrackID trackId, uint32_t slotIndex, void* rawInstance, const struct PluginDescriptor& plugDesc) override;

    // Sidechain Operations
    bool setTrackSidechainRouting(TrackID targetTrackId, uint32_t slotIndex, TrackID sourceTrackId, float sendGainLinear = 1.0f) override;
    void clearTrackSidechainRouting(TrackID targetTrackId, uint32_t slotIndex) override;
    bool getTrackSidechainRouting(TrackID targetTrackId, uint32_t slotIndex, TrackID& outSourceTrackId, float& outSendGainLinear) const override;

    // Callback Registration
    void registerMixerRoutingCallback(MixerRoutingCallback cb) override;

    IPlaylist* getPlaylist(TrackID id) override;
    IMIDISequencer* getMIDISequencer(TrackID id) override;
    IAutomationLaneManager* getAutomationManager(TrackID id) override;

    TrackPipelineDescriptor getPipelineDescriptor(TrackID id) const override;
    NodeID getTrackOutputNode(TrackID id) const override;

    // Overrides for new Track Queries
    std::vector<TrackID> getAllTrackIDs() const override;
    bool getTrackInfo(TrackID id, TrackCreateInfo& outInfo) const override;
    uint32_t getTrackIndexPosition(TrackID id) const override;
    TrackID getTrackParentFolderId(TrackID id) const override;
    std::atomic<uint64_t>* getRecordingStartSample(TrackID id) override;
    
    void renderMIDIPlayback(
        uint64_t startSample,
        uint32_t numSamples,
        bool loopEnabled,
        uint64_t loopStart,
        uint64_t loopEnd,
        class Layer2::IEventQueue* eventQueue,
        bool isPlaying
    ) override;

    void compileTimelineSnapshot() override;
    void setProjectSampleRate(uint32_t sampleRate) override;
    void recalculateTimeCaches(Layer2::ITempoService* tempoService) override;

    // IMidiClipDataProvider Overrides
    uint32_t getNotesInClip(ClipID clipId, ::MIDINote* outNotes, uint32_t maxNotes) const override;
    uint32_t getCCPointsInClip(ClipID clipId, ::MIDICCPoint* outPoints, uint32_t maxPoints) const override;
    uint32_t getPitchPointsInClip(ClipID clipId, ::MIDIPitchPoint* outPoints, uint32_t maxPoints) const override;

    void applyDelta(const ProjectDelta& delta, bool isUndo);

    // IArrangementManager Overrides
    std::vector<ArrangementInfo> getArrangements() const override;
    ArrangementID getActiveArrangement() const override;
    void setActiveArrangement(ArrangementID id) override;
    ArrangementID createArrangement(const char* name) override;
    void renameArrangement(ArrangementID id, const char* newName) override;
    void deleteArrangement(ArrangementID id) override;
    ArrangementID cloneArrangement(ArrangementID id, const char* cloneName) override;
    void mergeArrangements(
        ArrangementID sourceId,
        ArrangementID destId,
        int mergeMode,
        const MergeFilterOptions& filterOptions
    ) override;

private:
    std::unique_ptr<ITrackPipelineBuilder> builder_;
    ICommandHistory* commandHistory_;
    IAudioRegionSourceManager* sourceManager_;
    IDSPKernel* kernel_;
    Layer2::IMutationBridge* mutationBridge_;
    Layer3::IPluginManager* pluginManager_;
    NodeID masterChannelStripNode_;
    NodeID masterPluginSlotNode_ = NodeID::invalid();
    NodeID masterLatencyNode_ = NodeID::invalid();
    DSP::LatencyFactory* latencyFactory_;
    MixerRoutingCallback mixerRoutingCallback_;

    struct ArrangementState {
        std::unique_ptr<IPlaylist> playlist;
        std::unique_ptr<IMIDISequencer> midiSequencer;
        std::unique_ptr<IAutomationLaneManager> automationManager;
    };

    struct SendState {
        float gainLinear = 0.0f;
        float panPosition = 0.5f;
        bool isEnabled = false;
        NodeID destinationNodeId = NodeID::invalid();
        std::string destinationName = "-- Empty --";
    };

    struct SidechainRoutingState {
        TrackID sourceTrackId = TrackID::invalid();
        float sendGainLinear = 1.0f;
        bool isEnabled = false;
    };

    struct PluginState {
        uint32_t pluginId = 0;
        bool bypassed = false;
        void* pluginInstance = nullptr; // Layer3::IPlugin*
        char name[MAX_PLUGIN_NAME_LENGTH] = "";
    };

    PluginState masterPlugins_[8] = {};

    struct TrackState {
        TrackCreateInfo info;
        uint32_t indexPosition = 0;
        TrackID parentFolderId = {0, 0}; // 0 id means no parent
        TrackPipelineDescriptor pipeline;
        std::unordered_map<uint32_t, ArrangementState> arrangements; // Keyed by ArrangementID.id
        std::unique_ptr<std::atomic<uint64_t>> recordingStartSample;
        bool locked = false;


        // --- Mixer & Routing state ---
        float faderGain = 1.0f;
        float pan = 0.5f;
        bool mute = false;
        bool solo = false;
        SendState preSends[4];
        SendState postSends[4];
        uint32_t physicalInputIndex = 0;
        uint32_t inputChannelCount = 0;

        // --- Plugins state ---
        PluginState instrument;
        PluginState plugins[8];

        // --- Sidechain state ---
        SidechainRoutingState sidechains[8];
    };

    struct PluginStateCache {
        uint32_t nextId_ = 0;
        std::unordered_map<uint32_t, std::vector<uint8_t>> cache_;
        
        uint32_t storeState(std::vector<uint8_t> state) {
            uint32_t id = ++nextId_;
            cache_[id] = std::move(state);
            return id;
        }
        
        const std::vector<uint8_t>* getState(uint32_t id) const {
            auto it = cache_.find(id);
            if (it != cache_.end()) return &it->second;
            return nullptr;
        }
    };

    std::unordered_map<uint32_t, TrackState> tracks_;
    PluginStateCache pluginStateCache_;
    uint32_t nextIdCounter_ = 0;

    std::vector<ArrangementInfo> arrangements_;
    ArrangementID activeArrangementId_ = { 0xFFFFFFFFu };
    uint32_t nextArrangementIdCounter_ = 1;

    struct RTSequencerList {
        IMIDISequencer* midiSequencers[1024];
        IAutomationLaneManager* automationManagers[1024];
        bool isRecordArmed[1024];
        bool isInputMonitoring[1024];
        NodeID targetNodeIds[1024];
        uint32_t count = 0;
    };
    std::unique_ptr<RTSequencerList> rtSequencerListA_;
    std::unique_ptr<RTSequencerList> rtSequencerListB_;
    std::atomic<RTSequencerList*> rtSequencersActive_{nullptr};
    
    TimelineSnapshot pendingCompileSnapshot_;
    uint32_t projectSampleRate_ = 44100;
    std::vector<IPlaylist::RegionInfo> compilationScratch_;
    
    void syncRTSequencerList();
    
    TrackID generateNextId();
    TrackID createTrackInternal(const TrackCreateInfo& info);
    void deleteTrackInternal(TrackID id);
    void renameTrackInternal(TrackID id, uint32_t newNameId);
    void setTrackCommentsInternal(TrackID id, uint32_t commentsId);
    void setTrackOutputRoutingInternal(TrackID id, TrackID destinationTrackId);
    void moveTrackInternal(TrackID id, uint32_t newIndexPosition, TrackID newParentFolderId);
    void setTrackColorInternal(TrackID id, uint32_t newColorARGB);
    void setTrackRecordArmedInternal(TrackID id, bool armed);
    void setTrackInputMonitoringInternal(TrackID id, bool enabled);
    void setTrackTypeInternal(TrackID id, TrackType type);
    void setTrackLockedInternal(TrackID id, bool locked);



    // Internal Mixer/Routing/Plugin apply methods
    void setTrackFaderGainInternal(TrackID id, float gainLinear);
    void setTrackPanInternal(TrackID id, float panPosition);
    void setTrackMuteInternal(TrackID id, bool mute);
    void setTrackSoloInternal(TrackID id, bool solo);
    void setTrackSendGainInternal(TrackID id, bool isPreFader, uint32_t sendIndex, float gainLinear);
    void setTrackSendPanInternal(TrackID id, bool isPreFader, uint32_t sendIndex, float panPosition);
    void setTrackSendEnabledInternal(TrackID id, bool isPreFader, uint32_t sendIndex, bool enabled);
    void setTrackSendDestinationInternal(TrackID id, bool isPreFader, uint32_t sendIndex, NodeID destinationNodeId);
    void setTrackAudioInputChannelInternal(TrackID id, uint32_t mappedPhysicalInputIndex, uint32_t numChannels);
    void insertTrackPluginInternal(TrackID id, uint32_t slotIndex, uint32_t pluginId, uint32_t stateId = 0);
    void removeTrackPluginInternal(TrackID id, uint32_t slotIndex);
    void setTrackPluginBypassedInternal(TrackID id, uint32_t slotIndex, bool bypassed);
    void insertTrackInstrumentInternal(TrackID id, uint32_t pluginId, uint32_t stateId = 0);
    void removeTrackInstrumentInternal(TrackID id);
    void setTrackInstrumentBypassedInternal(TrackID id, bool bypassed);
    bool detectFeedbackCycle(TrackID sourceTrackId, TrackID targetTrackId) const override;
};

} // namespace composition
