#include "tracks/track_controller.h"
#include "DSP nodes/plugins/plugin_slot_node.h"

namespace bridge {

TrackController::TrackController(
    ISessionManager* sessionManager,
    Layer2::IMutationBridge* mutationBridge,
    Layer2::IStringRegistry* stringRegistry,
    IMeteringProvider* meteringProvider,
    Layer3::IPluginManager* pluginManager,
    NodeID masterChannelStripNode,
    NodeID masterBusNode,
    NodeID masterPluginSlotNode,
    NodeID masterLatencyNode,
    DSP::LatencyFactory* latencyFactory,
    Layer3::ITransport* transport,
    IAutomationRecordingGateway* recordingGateway,
    IHardwareSettingsFacade* hardwareFacade
) :
    ctx_{sessionManager, nullptr, mutationBridge, stringRegistry, meteringProvider, pluginManager,
         masterChannelStripNode, masterBusNode, masterPluginSlotNode, masterLatencyNode, latencyFactory, transport, recordingGateway, hardwareFacade,
         mutex_, sendCache_, trackParameterCache_,
         automationExpanded_, subLanesExpanded_, subLaneHeights_, trackSelection_, lastTweakedCache_, this}
{
    lifecycle_ = std::make_unique<TrackLifecycleController>(ctx_);
    mixer_ = std::make_unique<TrackMixerController>(ctx_);
    plugin_ = std::make_unique<TrackPluginController>(ctx_);
    routing_ = std::make_unique<TrackRoutingController>(ctx_);
    uiBuilder_ = std::make_unique<TrackUIStateBuilder>(ctx_);

    lifecycle_->setParameterCacheCallback([this](TrackID trackId, composition::ITrackManager* trackManager) {
        uiBuilder_->initializeTrackParameterCache(trackId, trackManager);
    });

    if (ctx_.sessionManager) {
        ctx_.sessionManager->registerChangeListener(this);
    }
}

TrackController::~TrackController() {
    if (ctx_.sessionManager) {
        ctx_.sessionManager->unregisterChangeListener(this);
    }
}

void TrackController::onSessionChanging() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (auto* trackManager = ctx_.getTrackManager()) {
        if (ctx_.meteringProvider) {
            for (auto id : trackManager->getAllTrackIDs()) {
                ctx_.meteringProvider->unregisterTrackNodeMapping(id);
            }
        }
    }
    uiBuilder_->clearParameterCache();
}

void TrackController::onSessionChanged(composition::IProjectSession* newSession) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (newSession && ctx_.meteringProvider) {
        if (auto* trackManager = newSession->getTrackManager()) {
            for (auto id : trackManager->getAllTrackIDs()) {
                auto desc = trackManager->getPipelineDescriptor(id);
                if (desc.trackNode.isValid()) {
                    ctx_.meteringProvider->registerTrackNodeMapping(id, desc.trackNode);
                }
            }
        }
    }
    if (newSession) {
        if (auto* trackManager = newSession->getTrackManager()) {
            trackManager->registerMixerRoutingCallback([this](TrackID trackId) {
                std::lock_guard<std::recursive_mutex> lock(mutex_);
                if (auto* tm = ctx_.getTrackManager()) {
                    uiBuilder_->initializeTrackParameterCache(trackId, tm);
                    
                    for (uint32_t i = 0; i < 4; ++i) {
                        auto& preCache = ctx_.getSendCache(trackId, true, i);
                        preCache.gainLinear = tm->getTrackSendGain(trackId, true, i);
                        preCache.isEnabled = tm->getTrackSendEnabled(trackId, true, i);
                        preCache.destinationNodeId = tm->getTrackSendDestination(trackId, true, i);
                        preCache.destinationName = tm->getTrackSendDestinationName(trackId, true, i);
                        
                        auto& postCache = ctx_.getSendCache(trackId, false, i);
                        postCache.gainLinear = tm->getTrackSendGain(trackId, false, i);
                        postCache.isEnabled = tm->getTrackSendEnabled(trackId, false, i);
                        postCache.destinationNodeId = tm->getTrackSendDestination(trackId, false, i);
                        postCache.destinationName = tm->getTrackSendDestinationName(trackId, false, i);
                    }


                }
            });
        }
    }
}

TrackID TrackController::addAudioTrack(const char* name, uint32_t channels, uint32_t colorARGB) {
    return lifecycle_->addAudioTrack(name, channels, colorARGB);
}
TrackID TrackController::addInstrumentTrack(const char* name, uint32_t colorARGB) {
    return lifecycle_->addInstrumentTrack(name, colorARGB);
}
TrackID TrackController::addAuxTrack(const char* name, uint32_t colorARGB) {
    return lifecycle_->addAuxTrack(name, colorARGB);
}
TrackID TrackController::addFolderTrack(const char* name, uint32_t colorARGB) {
    return lifecycle_->addFolderTrack(name, colorARGB);
}
void TrackController::removeTrack(TrackID trackId) {
    lifecycle_->removeTrack(trackId);
}
void TrackController::renameTrack(TrackID trackId, const char* name) {
    lifecycle_->renameTrack(trackId, name);
}
void TrackController::setTrackComments(TrackID trackId, const char* comments) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager || !ctx_.stringRegistry) return;

    uint32_t commentsId = ctx_.stringRegistry->registerString(comments);
    trackManager->setTrackComments(trackId, commentsId);
}
void TrackController::setTrackOutputRouting(TrackID trackId, TrackID destinationTrackId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* trackManager = ctx_.getTrackManager();
    if (!trackManager) return;

    trackManager->setTrackOutputRouting(trackId, destinationTrackId);
}
void TrackController::setTrackColor(TrackID trackId, uint32_t colorARGB) {
    lifecycle_->setTrackColor(trackId, colorARGB);
}
void TrackController::moveTrack(TrackID trackId, uint32_t newPositionIndex, TrackID newParentFolderId) {
    lifecycle_->moveTrack(trackId, newPositionIndex, newParentFolderId);
}
void TrackController::setTrackParentFolder(TrackID childTrackId, TrackID parentFolderId) {
    lifecycle_->setTrackParentFolder(childTrackId, parentFolderId);
}
void TrackController::setTrackMode(TrackID trackId, composition::TrackType mode) {
    lifecycle_->setTrackMode(trackId, mode);
}
TrackID TrackController::cloneTrack(TrackID sourceId) {
    return lifecycle_->cloneTrack(sourceId);
}
void TrackController::muteAllClips(TrackID trackId, bool mute) {
    lifecycle_->muteAllClips(trackId, mute);
}

void TrackController::setFaderGain(TrackID trackId, float gainLinear) {
    mixer_->setFaderGain(trackId, gainLinear);
}
void TrackController::setPan(TrackID trackId, float panPosition) {
    mixer_->setPan(trackId, panPosition);
}
void TrackController::setMute(TrackID trackId, bool mute) {
    mixer_->setMute(trackId, mute);
}
void TrackController::setSolo(TrackID trackId, bool solo) {
    mixer_->setSolo(trackId, solo);
}
void TrackController::setRecordArmed(TrackID trackId, bool armed) {
    mixer_->setRecordArmed(trackId, armed);
}
void TrackController::setInputMonitoring(TrackID trackId, bool enabled) {
    mixer_->setInputMonitoring(trackId, enabled);
}

void TrackController::setTrackInput(TrackID trackId, uint32_t optionId, uint32_t numChannels) {
    routing_->setTrackInput(trackId, optionId, numChannels);
}

void TrackController::setTrackAudioInput(TrackID trackId, uint32_t mappedPhysicalInputIndex, uint32_t numChannels) {
    setTrackInput(trackId, mappedPhysicalInputIndex, numChannels);
}

std::vector<TrackInputOption> TrackController::getAvailableTrackInputs(TrackID trackId) const {
    std::vector<TrackInputOption> options;
    auto state = getTrackState(trackId);
    if (state.type == composition::TrackType::INSTRUMENT || state.type == composition::TrackType::MIDI) {
        options.push_back({0xFFFFFFFF, 0, "All MIDI Inputs"});
        if (ctx_.hardwareFacade) {
            auto midiPorts = ctx_.hardwareFacade->getAvailableMidiPorts();
            for (const auto& port : midiPorts) {
                options.push_back({port.portIndex, 0, std::string(port.name)});
            }
        }
    } else if (state.type == composition::TrackType::AUDIO) {
        if (ctx_.hardwareFacade) {
            auto config = ctx_.hardwareFacade->getCurrentConfig();
            for (uint32_t i = 0; i < config.numInputChannels; ++i) {
                options.push_back({i, 1, "Input " + std::to_string(i + 1)});
            }
            for (uint32_t i = 0; i + 1 < config.numInputChannels; i += 2) {
                options.push_back({i, 2, "Input " + std::to_string(i + 1) + "-" + std::to_string(i + 2)});
            }
        }
    }
    return options;
}

std::vector<AudioInputChannelDescriptor> TrackController::getAvailableAudioInputs() const {
    std::vector<AudioInputChannelDescriptor> inputs;
    if (!ctx_.hardwareFacade) return inputs;
    
    auto config = ctx_.hardwareFacade->getCurrentConfig();
    for (uint32_t i = 0; i < config.numInputChannels; ++i) {
        inputs.push_back({i, 1, "Input " + std::to_string(i + 1)});
    }
    for (uint32_t i = 0; i + 1 < config.numInputChannels; i += 2) {
        inputs.push_back({i, 2, "Input " + std::to_string(i + 1) + "-" + std::to_string(i + 2)});
    }
    return inputs;
}

void TrackController::setSendGain(TrackID trackId, bool isPreFader, uint32_t sendIndex, float gainLinear) {
    routing_->setSendGain(trackId, isPreFader, sendIndex, gainLinear);
}
void TrackController::setSendPan(TrackID trackId, bool isPreFader, uint32_t sendIndex, float panPosition) {
    routing_->setSendPan(trackId, isPreFader, sendIndex, panPosition);
}
void TrackController::setSendEnabled(TrackID trackId, bool isPreFader, uint32_t sendIndex, bool enabled) {
    routing_->setSendEnabled(trackId, isPreFader, sendIndex, enabled);
}
void TrackController::setSendDestination(TrackID trackId, bool isPreFader, uint32_t sendIndex, NodeID destinationNodeId) {
    routing_->setSendDestination(trackId, isPreFader, sendIndex, destinationNodeId);
}

void TrackController::insertInstrument(TrackID trackId, uint32_t pluginId) {
    plugin_->insertInstrument(trackId, pluginId);
}
void TrackController::removeInstrument(TrackID trackId) {
    plugin_->removeInstrument(trackId);
}
void TrackController::setInstrumentBypassed(TrackID trackId, bool bypassed) {
    plugin_->setInstrumentBypassed(trackId, bypassed);
}
bool TrackController::openInstrumentEditor(TrackID trackId, void* parentWindow, int& outWidth, int& outHeight) {
    return plugin_->openInstrumentEditor(trackId, parentWindow, outWidth, outHeight);
}
void TrackController::closeInstrumentEditor(TrackID trackId) {
    plugin_->closeInstrumentEditor(trackId);
}

void TrackController::insertPlugin(TrackID trackId, uint32_t slotIndex, uint32_t pluginId) {
    plugin_->insertPlugin(trackId, slotIndex, pluginId);
}
void TrackController::removePlugin(TrackID trackId, uint32_t slotIndex) {
    plugin_->removePlugin(trackId, slotIndex);
}
void TrackController::setPluginBypassed(TrackID trackId, uint32_t slotIndex, bool bypassed) {
    plugin_->setPluginBypassed(trackId, slotIndex, bypassed);
}
std::vector<uint8_t> TrackController::getPluginState(TrackID trackId, uint32_t slotIndex) const {
    return plugin_->getPluginState(trackId, slotIndex);
}
void TrackController::setPluginState(TrackID trackId, uint32_t slotIndex, const std::vector<uint8_t>& state) {
    plugin_->setPluginState(trackId, slotIndex, state);
}
void TrackController::setPluginParameter(TrackID trackId, uint32_t slotIndex, uint32_t paramIndex, float value) {
    plugin_->setPluginParameter(trackId, slotIndex, paramIndex, value);
}
uint32_t TrackController::getPluginIdInSlot(TrackID trackId, uint32_t slotIndex) const {
    return plugin_->getPluginIdInSlot(trackId, slotIndex);
}
uint32_t TrackController::findPluginIdByName(std::string_view name) const {
    return plugin_->findPluginIdByName(name);
}
bool TrackController::openPluginEditor(TrackID trackId, uint32_t slotIndex, void* parentWindow, int& outWidth, int& outHeight) {
    return plugin_->openPluginEditor(trackId, slotIndex, parentWindow, outWidth, outHeight);
}
void TrackController::closePluginEditor(TrackID trackId, uint32_t slotIndex) {
    plugin_->closePluginEditor(trackId, slotIndex);
}

void TrackController::subscribeToPluginParameterTweaks(TrackID trackId, uint32_t slotIndex, bool isInstrument, PluginParameterTweakedCallback cb) {
    plugin_->subscribeToPluginParameterTweaks(trackId, slotIndex, isInstrument, cb);
}

void TrackController::unsubscribeFromPluginParameterTweaks(TrackID trackId, uint32_t slotIndex, bool isInstrument) {
    plugin_->unsubscribeFromPluginParameterTweaks(trackId, slotIndex, isInstrument);
}

void TrackController::setPluginSidechainSource(TrackID targetTrackId, uint32_t slotIndex, TrackID sourceTrackId, float sendGaindB) {
    routing_->setPluginSidechainSource(targetTrackId, slotIndex, sourceTrackId, sendGaindB);
}

void TrackController::clearPluginSidechainSource(TrackID targetTrackId, uint32_t slotIndex) {
    routing_->clearPluginSidechainSource(targetTrackId, slotIndex);
}

std::vector<TrackInputOption> TrackController::getAvailableSidechainSources(TrackID targetTrackId) const {
    return routing_->getAvailableSidechainSources(targetTrackId);
}

SidechainSlotUIState TrackController::getPluginSidechainState(TrackID targetTrackId, uint32_t slotIndex) const {
    return routing_->getPluginSidechainState(targetTrackId, slotIndex);
}

void TrackController::completeInstrumentInsertion(TrackID trackId, void* rawInstance, const PluginDescriptor& plugDesc) {
    plugin_->completeInstrumentInsertion(trackId, rawInstance, plugDesc);
}

NodeID TrackController::getMasterChannelStripNode() const {
    return ctx_.masterChannelStripNode;
}

std::vector<ParameterDescriptorCacheItem> TrackController::getCachedParameters(TrackID trackId) const {
    return uiBuilder_->getCachedParameters(trackId);
}
uint32_t TrackController::getTrackCount() const {
    return uiBuilder_->getTrackCount();
}
TrackUIState TrackController::getTrackState(TrackID trackId) const {
    return uiBuilder_->getTrackState(trackId);
}
std::vector<TrackUIState> TrackController::getAllTracks() const {
    return uiBuilder_->getAllTracks();
}
TrackDynamicState TrackController::getDynamicState(NodeID channelStripNode) const {
    return uiBuilder_->getDynamicState(channelStripNode);
}
std::vector<PluginDescriptor> TrackController::getAvailablePlugins() const {
    return uiBuilder_->getAvailablePlugins();
}

void TrackController::setTrackLocked(TrackID id, bool locked) {
    if (auto* trackManager = ctx_.getTrackManager()) {
        trackManager->setTrackLocked(id, locked);
    }
}
void TrackController::setAutomationExpanded(TrackID id, bool expanded) {
    uiBuilder_->setAutomationExpanded(id, expanded);
}
void TrackController::setTakesExpanded(TrackID id, bool expanded) {
    if (auto* trackManager = ctx_.getTrackManager()) {
        trackManager->setTrackTakesExpanded(id, expanded);
    }
}
void TrackController::setAutomationSubLaneExpanded(TrackID id, uint32_t subLaneIndex, bool expanded) {
    uiBuilder_->setAutomationSubLaneExpanded(id, subLaneIndex, expanded);
}
void TrackController::setAutomationSubLaneHeight(TrackID id, uint32_t subLaneIndex, uint32_t heightPx) {
    uiBuilder_->setAutomationSubLaneHeight(id, subLaneIndex, heightPx);
}
void TrackController::setTrackSelected(TrackID id, bool selected) {
    uiBuilder_->setTrackSelected(id, selected);
}
void TrackController::clearTrackSelection() {
    uiBuilder_->clearTrackSelection();
}

void TrackController::setAutomationLaneRequestCallback(AutomationLaneRequestCallback cb) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    automationLaneCallback_ = std::move(cb);
}

void TrackController::requestAutomationLaneForLastTweaked(TrackID trackId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = lastTweakedCache_.find(trackId.toRaw());
    if (it != lastTweakedCache_.end() && it->second.isValid && automationLaneCallback_) {
        automationLaneCallback_(trackId, it->second.routingNodeId, it->second.subNodeId, it->second.paramIndex);
    }
}

} // namespace bridge
