#include "iproject_session.h"
#include "project_serializer.h"
#include "project_state_bridge.h"
#include "project_json_serializer.h"
#include "musical_composition/track_manager/track_manager_impl.h"
#include "musical_composition/playlist/playlist_impl.h"
#include "musical_composition/playlist/region_operations.h"
#include "musical_composition/midi_sequencer/midi_sequencer_impl.h"
#include "musical_composition/automation/automation_commands.h"
#include "musical_composition/automation/automation_lane_manager_impl.h"
#include "musical_composition/command_history/icommand_history.h"
#include "musical_composition/command_history/command_history_impl.h"
#include "musical_composition/region_manager/region_source_manager_impl.h"
#include "musical_composition/arranger/arranger_track_impl.h"
#include "musical_composition/chord_track/chord_track_impl.h"
#include "musical_composition/clipboard/clipboard_impl.h"
#include "musical_composition/comping/comping_engine_impl.h"
#include "marker_manager_impl.h"
#include "key_signature_map_impl.h"
#include "region_metadata_manager_impl.h"
#include "Core infrastructure/bridges/imutation_bridge.h"
#include "Core audio engine/plugin/iplugin_manager.h"
#include "metadata_commands.h"
#include "musical_composition/region_manager/source_manager_commands.h"
#include <fstream>

namespace composition {

class ProjectSessionImpl : public IProjectSession {
private:
    ProjectMetadata metadata_;
    MixStatistics mixStats_;
    
    std::unique_ptr<ICommandHistory> commandHistory_;
    std::unique_ptr<ITrackManager> trackManager_;
    std::unique_ptr<IArrangerTrack> arrangerTrack_;
    std::unique_ptr<IChordTrack> chordTrack_;
    std::unique_ptr<IAudioRegionSourceManager> regionSourceManager_;
    std::unique_ptr<IClipboard> clipboard_;
    std::unique_ptr<ICompingEngine> compingEngine_;
    std::unique_ptr<IMarkerManager> markerManager_;
    std::unique_ptr<IKeySignatureMap> keySignatureMap_;
    std::unique_ptr<IRegionMetadataManager> regionMetadataManager_;
    Layer3::IPluginManager* pluginManager_;

public:
    ProjectSessionImpl(
        std::unique_ptr<ITrackPipelineBuilder> builder,
        IDSPKernel* kernel,
        Layer2::IMutationBridge* mutationBridge,
        Layer3::IPluginManager* pluginManager,
        NodeID masterChannelStripNode,
        NodeID masterPluginSlotNode,
        NodeID masterLatencyNode,
        DSP::LatencyFactory* latencyFactory
    ) : pluginManager_(pluginManager) {
        // 1. Core Services
        auto history = std::make_unique<CommandHistoryImpl>();
        commandHistory_ = std::move(history);
        commandHistory_->setOnHistoryChangedCallback([this]() {
            this->mixStats_.isAnalyzed = false;
        });
        
        regionSourceManager_ = std::make_unique<RegionSourceManagerImpl>(commandHistory_.get());
        clipboard_ = std::make_unique<ClipboardImpl>();
        compingEngine_ = std::make_unique<CompingEngineImpl>(commandHistory_.get());
        markerManager_ = std::make_unique<MarkerManagerImpl>(commandHistory_.get());
        keySignatureMap_ = std::make_unique<KeySignatureMapImpl>(commandHistory_.get());
        regionMetadataManager_ = std::make_unique<RegionMetadataManagerImpl>(commandHistory_.get());
        
        // 2. Structural Tracks
        arrangerTrack_ = std::make_unique<ArrangerTrackImpl>(commandHistory_.get());
        chordTrack_ = std::make_unique<ChordTrackImpl>(commandHistory_.get());
        
        // 3. Track Manager (Orchestration)
        trackManager_ = std::make_unique<TrackManagerImpl>(
            std::move(builder), 
            commandHistory_.get(), 
            regionSourceManager_.get(),
            kernel,
            mutationBridge,
            pluginManager,
            masterChannelStripNode,
            masterPluginSlotNode,
            masterLatencyNode,
            latencyFactory
        );

        // 4. Register Delta Handlers for Undo/Redo
        auto* historyPtr = static_cast<CommandHistoryImpl*>(commandHistory_.get());
        
        historyPtr->registerHandler(SubsystemID::PLAYLIST, [this](const ProjectDelta& d, bool u){ 
            IPlaylist* p = nullptr;
            if (d.operationType == PlaylistOps::ADD_REGION || d.operationType == PlaylistOps::REMOVE_REGION) {
                p = trackManager_->getPlaylist(uint64ToHandle<TrackID>(d.targetId));
            } else {
                RegionID rid = uint64ToHandle<RegionID>(d.targetId);
                for (auto tid : trackManager_->getAllTrackIDs()) {
                    if (auto* pl = trackManager_->getPlaylist(tid)) {
                        std::vector<IPlaylist::RegionInfo> regions(128);
                        uint32_t count = pl->getAllRegions(regions.data(), static_cast<uint32_t>(regions.size()));
                        if (count > regions.size()) {
                            regions.resize(count);
                            pl->getAllRegions(regions.data(), static_cast<uint32_t>(regions.size()));
                        }
                        for (uint32_t i = 0; i < count; ++i) {
                            if (regions[i].id == rid) {
                                p = pl;
                                break;
                            }
                        }
                    }
                    if (p) break;
                }
            }
            if (p) {
                static_cast<PlaylistImpl*>(p)->applyDelta(d, u);
                static_cast<TrackManagerImpl*>(trackManager_.get())->compileTimelineSnapshot();
            }
        });

        historyPtr->registerHandler(SubsystemID::MIDI_SEQUENCER, [this](const ProjectDelta& d, bool u){ 
            auto* s = trackManager_->getMIDISequencer(uint64ToHandle<TrackID>(d.targetId));
            if (s) static_cast<MIDISequencerImpl*>(s)->applyDelta(d, u);
        });

        historyPtr->registerHandler(SubsystemID::AUTOMATION, [this](const ProjectDelta& d, bool u){ 
            IAutomationLaneManager* a = nullptr;
            if (d.operationType == AutomationOps::CREATE_LANE || d.operationType == AutomationOps::REMOVE_LANE) {
                a = trackManager_->getAutomationManager(uint64ToHandle<TrackID>(d.targetId));
            } else {
                NodeID targetNode = uint64ToHandle<NodeID>(d.targetId);
                for (auto tid : trackManager_->getAllTrackIDs()) {
                    TrackCreateInfo info;
                    if (trackManager_->getTrackInfo(tid, info)) {
                        if (trackManager_->getTrackOutputNode(tid) == targetNode) {
                            a = trackManager_->getAutomationManager(tid);
                            break;
                        }
                        if (auto* mgr = trackManager_->getAutomationManager(tid)) {
                            auto& lanes = static_cast<AutomationLaneManagerImpl*>(mgr)->getLanes();
                            for (const auto& [target, lane] : lanes) {
                                if (target.nodeId == targetNode) {
                                    a = mgr;
                                    break;
                                }
                            }
                        }
                    }
                    if (a) break;
                }
            }
            if (a) static_cast<AutomationLaneManagerImpl*>(a)->applyDelta(d, u);
        });

        historyPtr->registerHandler(SubsystemID::TRACK_MANAGER, [this](const ProjectDelta& d, bool u){ 
            static_cast<TrackManagerImpl*>(trackManager_.get())->applyDelta(d, u);
        });

        historyPtr->registerHandler(SubsystemID::ARRANGER, [this](const ProjectDelta& d, bool u){ 
            static_cast<ArrangerTrackImpl*>(arrangerTrack_.get())->applyDelta(d, u);
        });

        historyPtr->registerHandler(SubsystemID::CHORD_TRACK, [this](const ProjectDelta& d, bool u){ 
            static_cast<ChordTrackImpl*>(chordTrack_.get())->applyDelta(d, u);
        });

        historyPtr->registerHandler(SubsystemID::COMPING, [this](const ProjectDelta& d, bool u){ 
            static_cast<CompingEngineImpl*>(compingEngine_.get())->applyDelta(d, u);
        });

        historyPtr->registerHandler(SubsystemID::MIXER_ROUTING, [this](const ProjectDelta& d, bool u){ 
            static_cast<TrackManagerImpl*>(trackManager_.get())->applyDelta(d, u);
        });

        historyPtr->registerHandler(SubsystemID::MARKER_TRACK, [this](const ProjectDelta& d, bool u){ 
            static_cast<MarkerManagerImpl*>(markerManager_.get())->applyDelta(d, u);
        });

        historyPtr->registerHandler(SubsystemID::KEY_SIGNATURE_MAP, [this](const ProjectDelta& d, bool u){ 
            static_cast<KeySignatureMapImpl*>(keySignatureMap_.get())->applyDelta(d, u);
        });

        historyPtr->registerHandler(SubsystemID::SOURCE_MANAGER, [this](const ProjectDelta& d, bool u){ 
            static_cast<RegionSourceManagerImpl*>(regionSourceManager_.get())->applyDelta(d, u);
        });

        historyPtr->registerHandler(SubsystemID::REGION_METADATA, [this](const ProjectDelta& d, bool u){ 
            static_cast<RegionMetadataManagerImpl*>(regionMetadataManager_.get())->applyDelta(d, u);
        });

        historyPtr->registerHandler(SubsystemID::PROJECT_METADATA, [this](const ProjectDelta& d, bool u){ 
            if (d.operationType == ProjectMetadataOps::SET_METADATA) {
                ProjectMetadataPayload payload{};
                std::memcpy(&payload, u ? d.oldState : d.newState, sizeof(ProjectMetadataPayload));
                this->metadata_ = fromPayload(payload);
            }
        });
        trackManager_->setProjectSampleRate(metadata_.sampleRate);
    }

    ITrackManager* getTrackManager() override { return trackManager_.get(); }
    IArrangementManager* getArrangementManager() override { return static_cast<IArrangementManager*>(static_cast<TrackManagerImpl*>(trackManager_.get())); }
    ICommandHistory* getCommandHistory() override { return commandHistory_.get(); }
    IArrangerTrack* getArrangerTrack() override { return arrangerTrack_.get(); }
    IChordTrack* getChordTrack() override { return chordTrack_.get(); }
    IAudioRegionSourceManager* getRegionSourceManager() override { return regionSourceManager_.get(); }
    IClipboard* getClipboard() override { return clipboard_.get(); }
    ICompingEngine* getCompingEngine() override { return compingEngine_.get(); }
    IMarkerManager* getMarkerManager() override { return markerManager_.get(); }
    IKeySignatureMap* getKeySignatureMap() override { return keySignatureMap_.get(); }
    IRegionMetadataManager* getRegionMetadataManager() override { return regionMetadataManager_.get(); }

    const ProjectMetadata& getMetadata() const override { return metadata_; }
    void setMetadata(const ProjectMetadata& meta) override {
        if (commandHistory_ && metadata_.sampleRate != 0) {
            bool changed = (metadata_.projectName != meta.projectName) ||
                           (metadata_.author != meta.author) ||
                           (metadata_.sampleRate != meta.sampleRate) ||
                           (metadata_.initialTempoBPM != meta.initialTempoBPM) ||
                           (metadata_.timeSignatureNumerator != meta.timeSignatureNumerator) ||
                           (metadata_.timeSignatureDenominator != meta.timeSignatureDenominator) ||
                           (metadata_.targetBitDepth != meta.targetBitDepth) ||
                           (metadata_.sessionDurationSeconds != meta.sessionDurationSeconds);

            if (changed) {
                ProjectDelta delta{};
                delta.subsystemId = SubsystemID::PROJECT_METADATA;
                delta.operationType = ProjectMetadataOps::SET_METADATA;
                delta.targetId = 0;

                ProjectMetadataPayload oldPayload = toPayload(metadata_);
                ProjectMetadataPayload newPayload = toPayload(meta);

                delta.oldStateSize = sizeof(ProjectMetadataPayload);
                std::memcpy(delta.oldState, &oldPayload, sizeof(ProjectMetadataPayload));

                delta.newStateSize = sizeof(ProjectMetadataPayload);
                std::memcpy(delta.newState, &newPayload, sizeof(ProjectMetadataPayload));

                commandHistory_->pushDelta(delta);
            }
        }
        metadata_ = meta;
        if (trackManager_) {
            trackManager_->setProjectSampleRate(metadata_.sampleRate);
        }
    }

    MixStatistics getMixStatistics() const override { return mixStats_; }
    void setMixStatistics(const MixStatistics& stats) override { mixStats_ = stats; }

    bool saveToFile(const std::string& absolutePath, Layer2::IStringRegistry* stringRegistry = nullptr) override {
        ProjectState state = ProjectStateBridge::extract(*this, trackManager_.get(), stringRegistry, absolutePath);
        std::vector<uint8_t> buffer;
        if (!ProjectSerializer::serialize(state, buffer)) return false;
        
        std::ofstream file(absolutePath, std::ios::binary);
        if (!file.is_open()) return false;
        
        file.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        return true;
    }

    bool loadFromFile(const std::string& absolutePath, Layer2::IStringRegistry* stringRegistry = nullptr, std::vector<MissingPluginReport>* outMissingPlugins = nullptr) override {
        std::ifstream file(absolutePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return false;
        
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        if (size < 0) return false;
        std::vector<uint8_t> buffer(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) return false;
        
        ProjectState state;
        if (!ProjectSerializer::deserialize(buffer, state)) return false;
        return ProjectStateBridge::restore(state, *this, trackManager_.get(), stringRegistry, pluginManager_, outMissingPlugins);
    }

    bool saveToJsonFile(const std::string& absolutePath, Layer2::IStringRegistry* stringRegistry = nullptr) override {
        ProjectState state = ProjectStateBridge::extract(*this, trackManager_.get(), stringRegistry, absolutePath);
        std::string jsonStr;
        if (!ProjectJsonSerializer::serialize(state, jsonStr)) return false;
        
        std::ofstream file(absolutePath);
        if (!file.is_open()) return false;
        
        file << jsonStr;
        return true;
    }

    bool loadFromJsonFile(const std::string& absolutePath, Layer2::IStringRegistry* stringRegistry = nullptr, std::vector<MissingPluginReport>* outMissingPlugins = nullptr) override {
        std::ifstream file(absolutePath);
        if (!file.is_open()) return false;
        
        std::string jsonStr((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        ProjectState state;
        if (!ProjectJsonSerializer::deserialize(jsonStr, state)) return false;
        return ProjectStateBridge::restore(state, *this, trackManager_.get(), stringRegistry, pluginManager_, outMissingPlugins);
    }
};

std::unique_ptr<IProjectSession> IProjectSession::create(
    std::unique_ptr<ITrackPipelineBuilder> builder,
    IDSPKernel* kernel,
    Layer2::IMutationBridge* mutationBridge,
    Layer3::IPluginManager* pluginManager,
    NodeID masterChannelStripNode,
    NodeID masterPluginSlotNode,
    NodeID masterLatencyNode,
    DSP::LatencyFactory* latencyFactory
) {
    return std::make_unique<ProjectSessionImpl>(std::move(builder), kernel, mutationBridge, pluginManager, masterChannelStripNode, masterPluginSlotNode, masterLatencyNode, latencyFactory);
}

} // namespace composition
