#pragma once
#include "tracks/itrack_controller.h"
#include "telemetry/imetering_provider.h"
#include "project/isession_manager.h"
#include "Core infrastructure/bridges/imutation_bridge.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include "automation/iautomation_recording_gateway.h"
#include "tracks/track_lifecycle_controller.h"
#include "tracks/track_mixer_controller.h"
#include "tracks/track_plugin_controller.h"
#include "tracks/track_routing_controller.h"
#include "tracks/track_ui_state_builder.h"
#include "tracks/track_controller_context.h"
#include <mutex>
#include <memory>
#include <vector>

namespace Layer3 { class IPluginManager; class ITransport; }
namespace DSP   { class LatencyFactory; }

namespace bridge {

class TrackController : public ITrackController, public ISessionChangeListener {
public:
    TrackController(
        ISessionManager* sessionManager,
        Layer2::IMutationBridge* mutationBridge,
        Layer2::IStringRegistry* stringRegistry,
        IMeteringProvider* meteringProvider,
        Layer3::IPluginManager* pluginManager = nullptr,
        NodeID masterChannelStripNode = NodeID::invalid(),
        NodeID masterBusNode = NodeID::invalid(),
        NodeID masterPluginSlotNode = NodeID::invalid(),
        NodeID masterLatencyNode = NodeID::invalid(),
        DSP::LatencyFactory* latencyFactory = nullptr,
        Layer3::ITransport* transport = nullptr,
        IAutomationRecordingGateway* recordingGateway = nullptr,
        IHardwareSettingsFacade* hardwareFacade = nullptr
    );
    ~TrackController() override;

    void setRecordingController(IRecordingController* rc) { ctx_.recordingController = rc; }

    TrackID addAudioTrack(const char* name, uint32_t channels, uint32_t colorARGB) override;
    TrackID addInstrumentTrack(const char* name, uint32_t colorARGB) override;
    TrackID addAuxTrack(const char* name, uint32_t colorARGB) override;
    TrackID addFolderTrack(const char* name, uint32_t colorARGB) override;
    void removeTrack(TrackID trackId) override;
    void renameTrack(TrackID trackId, const char* name) override;
    void setTrackComments(TrackID trackId, const char* comments) override;
    void setTrackOutputRouting(TrackID trackId, TrackID destinationTrackId) override;
    void setTrackColor(TrackID trackId, uint32_t colorARGB) override;
    void moveTrack(TrackID trackId, uint32_t newPositionIndex, TrackID newParentFolderId) override;
    void setTrackParentFolder(TrackID childTrackId, TrackID parentFolderId) override;
    void setTrackMode(TrackID trackId, composition::TrackType mode) override;
    TrackID cloneTrack(TrackID sourceId) override;
    void muteAllClips(TrackID trackId, bool mute) override;

    void setFaderGain(TrackID trackId, float gainLinear) override;
    void setPan(TrackID trackId, float panPosition) override;
    void setMute(TrackID trackId, bool mute) override;
    void setSolo(TrackID trackId, bool solo) override;
    void setRecordArmed(TrackID trackId, bool armed) override;
    void setInputMonitoring(TrackID trackId, bool enabled) override;

    void setTrackInput(TrackID trackId, uint32_t optionId, uint32_t numChannels) override;
    std::vector<TrackInputOption> getAvailableTrackInputs(TrackID trackId) const override;
    void setTrackAudioInput(TrackID trackId, uint32_t mappedPhysicalInputIndex, uint32_t numChannels) override;
    std::vector<AudioInputChannelDescriptor> getAvailableAudioInputs() const override;


    void setSendGain(TrackID trackId, bool isPreFader, uint32_t sendIndex, float gainLinear) override;
    void setSendPan(TrackID trackId, bool isPreFader, uint32_t sendIndex, float panPosition) override;
    void setSendEnabled(TrackID trackId, bool isPreFader, uint32_t sendIndex, bool enabled) override;
    void setSendDestination(TrackID trackId, bool isPreFader, uint32_t sendIndex, NodeID destinationNodeId) override;

    void insertInstrument(TrackID trackId, uint32_t pluginId) override;
    void removeInstrument(TrackID trackId) override;
    void setInstrumentBypassed(TrackID trackId, bool bypassed) override;
    bool openInstrumentEditor(TrackID trackId, void* parentWindow, int& outWidth, int& outHeight) override;
    void closeInstrumentEditor(TrackID trackId) override;

    void insertPlugin(TrackID trackId, uint32_t slotIndex, uint32_t pluginId) override;
    void removePlugin(TrackID trackId, uint32_t slotIndex) override;
    void setPluginBypassed(TrackID trackId, uint32_t slotIndex, bool bypassed) override;
    std::vector<uint8_t> getPluginState(TrackID trackId, uint32_t slotIndex) const override;
    void setPluginState(TrackID trackId, uint32_t slotIndex, const std::vector<uint8_t>& state) override;
    void setPluginParameter(TrackID trackId, uint32_t slotIndex, uint32_t paramIndex, float value) override;
    uint32_t getPluginIdInSlot(TrackID trackId, uint32_t slotIndex) const override;
    uint32_t findPluginIdByName(std::string_view name) const override;
    bool openPluginEditor(TrackID trackId, uint32_t slotIndex, void* parentWindow, int& outWidth, int& outHeight) override;
    void closePluginEditor(TrackID trackId, uint32_t slotIndex) override;

    void subscribeToPluginParameterTweaks(TrackID trackId, uint32_t slotIndex, bool isInstrument, PluginParameterTweakedCallback cb) override;
    void unsubscribeFromPluginParameterTweaks(TrackID trackId, uint32_t slotIndex, bool isInstrument) override;

    void setPluginSidechainSource(TrackID targetTrackId, uint32_t slotIndex, TrackID sourceTrackId, float sendGaindB = 0.0f) override;
    void clearPluginSidechainSource(TrackID targetTrackId, uint32_t slotIndex) override;
    std::vector<TrackInputOption> getAvailableSidechainSources(TrackID targetTrackId) const override;
    SidechainSlotUIState getPluginSidechainState(TrackID targetTrackId, uint32_t slotIndex) const override;
    
    void completeInstrumentInsertion(TrackID trackId, void* rawInstance, const PluginDescriptor& plugDesc);

    NodeID getMasterChannelStripNode() const override;

    std::vector<ParameterDescriptorCacheItem> getCachedParameters(TrackID trackId) const override;
    uint32_t getTrackCount() const override;
    TrackUIState getTrackState(TrackID trackId) const override;
    std::vector<TrackUIState> getAllTracks() const override;
    TrackDynamicState getDynamicState(NodeID channelStripNode) const override;
    std::vector<PluginDescriptor> getAvailablePlugins() const override;
    void setTrackLocked(TrackID id, bool locked) override;
    void setAutomationExpanded(TrackID id, bool expanded) override;
    void setTakesExpanded(TrackID id, bool expanded) override;
    void setAutomationSubLaneExpanded(TrackID id, uint32_t subLaneIndex, bool expanded) override;
    void setAutomationSubLaneHeight(TrackID id, uint32_t subLaneIndex, uint32_t heightPx) override;
    void setTrackSelected(TrackID id, bool selected) override;
    void clearTrackSelection() override;

    void setAutomationLaneRequestCallback(AutomationLaneRequestCallback cb) override;
    void requestAutomationLaneForLastTweaked(TrackID trackId) override;

    void onSessionChanging() override;
    void onSessionChanged(composition::IProjectSession* newSession) override;

private:
    std::recursive_mutex mutex_;
    std::unordered_map<SendCacheKey, SendSlotCache, SendCacheKeyHash> sendCache_;
    std::unordered_map<uint64_t, std::vector<ParameterDescriptorCacheItem>> trackParameterCache_;
    std::unordered_map<uint64_t, bool> automationExpanded_;
    std::unordered_map<uint64_t, std::vector<bool>> subLanesExpanded_;
    std::unordered_map<uint64_t, std::vector<uint32_t>> subLaneHeights_;
    std::unordered_map<uint64_t, bool> trackSelection_;
    std::unordered_map<uint64_t, LastTweakedParameter> lastTweakedCache_;
    AutomationLaneRequestCallback automationLaneCallback_;
    TrackControllerContext ctx_;

    std::unique_ptr<TrackLifecycleController> lifecycle_;
    std::unique_ptr<TrackMixerController> mixer_;
    std::unique_ptr<TrackPluginController> plugin_;
    std::unique_ptr<TrackRoutingController> routing_;
    std::unique_ptr<TrackUIStateBuilder> uiBuilder_;
};

} // namespace bridge
