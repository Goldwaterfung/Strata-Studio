#pragma once
#include "musical_composition/musical_primitives.h"
#include "musical_composition/interfaces/track_pipeline_descriptor.h"
#include <vector>
#include <functional>
#include <string>

namespace Layer2 {
class IEventQueue;
class ITempoService;
}

namespace composition {

using MixerRoutingCallback = std::function<void(TrackID trackId)>;

enum class TrackType : uint8_t {
    AUDIO, MIDI, INSTRUMENT, AUX, MASTER, FOLDER
};

struct TrackCreateInfo {
    TrackType type;
    uint32_t nameId;           // IStringRegistry ID
    uint32_t colorARGB;
    uint32_t audioChannelCount; // e.g., 1 (mono), 2 (stereo)
    bool isRecordArmed;
    bool isInputMonitoring;
    bool isTakesExpanded = false;
    TrackID trackId = TrackID::invalid();
    std::atomic<uint64_t>* recordingStartSample = nullptr;
    uint32_t commentsId = 0;                          // IStringRegistry ID (0 = Empty)
    TrackID outputTargetTrackId = TrackID::invalid(); // Destination Track ID (invalid = Master Bus)
    uint32_t inputSourceIndex = 0;                    // Input physical index (0 = None/Default)
};

class IPlaylist;
class IMIDISequencer;
struct AutomationTarget;

class ITrackManager {
public:
    virtual ~ITrackManager() = default;

    // Track Lifecycle
    virtual TrackID createTrack(const TrackCreateInfo& info) = 0;
    virtual void deleteTrack(TrackID id) = 0;

    // Track Operations
    virtual void renameTrack(TrackID id, uint32_t newNameId) = 0;
    virtual void setTrackComments(TrackID id, uint32_t commentsId) = 0;
    virtual void setTrackOutputRouting(TrackID id, TrackID destinationTrackId) = 0;
    virtual void moveTrack(TrackID id, uint32_t newIndexPosition, TrackID newParentFolderId) = 0;
    virtual void setTrackColor(TrackID id, uint32_t newColorARGB) = 0;
    virtual void setTrackRecordArmed(TrackID id, bool armed) = 0;
    virtual void setTrackInputMonitoring(TrackID id, bool enabled) = 0;
    virtual void setTrackType(TrackID id, TrackType type) = 0;
    virtual void setTrackTakesExpanded(TrackID id, bool expanded) = 0;
    virtual void setTrackLocked(TrackID id, bool locked) = 0;
    virtual bool isTrackLocked(TrackID id) const = 0;


    // Subsystem getters (RT-safe pointers)
    virtual IPlaylist* getPlaylist(TrackID id) = 0;
    virtual IMIDISequencer* getMIDISequencer(TrackID id) = 0;
    virtual class IAutomationLaneManager* getAutomationManager(TrackID id) = 0;

    // DSP / Layer 3 bindings
    virtual TrackPipelineDescriptor getPipelineDescriptor(TrackID id) const = 0;
    virtual NodeID getTrackOutputNode(TrackID id) const = 0;

    // Track Queries (added for Presentation Layer support)
    virtual std::vector<TrackID> getAllTrackIDs() const = 0;
    virtual bool getTrackInfo(TrackID id, TrackCreateInfo& outInfo) const = 0;
    virtual uint32_t getTrackIndexPosition(TrackID id) const = 0;
    virtual TrackID getTrackParentFolderId(TrackID id) const = 0;
    virtual std::atomic<uint64_t>* getRecordingStartSample(TrackID trackId) = 0;

    // Real-Time Playback Rendering
    virtual void renderMIDIPlayback(
        uint64_t startSample,
        uint32_t numSamples,
        bool loopEnabled,
        uint64_t loopStart,
        uint64_t loopEnd,
        class Layer2::IEventQueue* eventQueue,
        bool isPlaying
    ) = 0;

    // Timeline snapshot compilation
    virtual void compileTimelineSnapshot() = 0;
    virtual void setProjectSampleRate(uint32_t sampleRate) = 0;

    // Mixer Operations
    virtual void setTrackFaderGain(TrackID id, float gainLinear) = 0;
    virtual void setTrackPan(TrackID id, float panPosition) = 0;
    virtual void setTrackMute(TrackID id, bool mute) = 0;
    virtual void setTrackSolo(TrackID id, bool solo) = 0;

    // Mixer Queries
    virtual float getTrackFaderGain(TrackID id) const = 0;
    virtual float getTrackPan(TrackID id) const = 0;
    virtual bool getTrackMute(TrackID id) const = 0;
    virtual bool getTrackSolo(TrackID id) const = 0;

    // Routing Operations
    virtual void setTrackSendGain(TrackID id, bool isPreFader, uint32_t sendIndex, float gainLinear) = 0;
    virtual void setTrackSendPan(TrackID id, bool isPreFader, uint32_t sendIndex, float panPosition) = 0;
    virtual void setTrackSendEnabled(TrackID id, bool isPreFader, uint32_t sendIndex, bool enabled) = 0;
    virtual void setTrackSendDestination(TrackID id, bool isPreFader, uint32_t sendIndex, NodeID destinationNodeId) = 0;
    virtual void setTrackAudioInputChannel(TrackID id, uint32_t mappedPhysicalInputIndex, uint32_t numChannels) = 0;

    // Routing Queries
    virtual float getTrackSendGain(TrackID id, bool isPreFader, uint32_t sendIndex) const = 0;
    virtual float getTrackSendPan(TrackID id, bool isPreFader, uint32_t sendIndex) const = 0;
    virtual bool getTrackSendEnabled(TrackID id, bool isPreFader, uint32_t sendIndex) const = 0;
    virtual NodeID getTrackSendDestination(TrackID id, bool isPreFader, uint32_t sendIndex) const = 0;
    virtual std::string getTrackSendDestinationName(TrackID id, bool isPreFader, uint32_t sendIndex) const = 0;

    // Plugin Operations
    virtual void insertTrackPlugin(TrackID id, uint32_t slotIndex, uint32_t pluginId) = 0;
    virtual void removeTrackPlugin(TrackID id, uint32_t slotIndex) = 0;
    virtual void setTrackPluginBypassed(TrackID id, uint32_t slotIndex, bool bypassed) = 0;
    virtual void insertTrackInstrument(TrackID id, uint32_t pluginId) = 0;
    virtual void removeTrackInstrument(TrackID id) = 0;
    virtual void setTrackInstrumentBypassed(TrackID id, bool bypassed) = 0;
    virtual void completeTrackInstrumentInsertion(TrackID trackId, void* rawInstance, const struct PluginDescriptor& plugDesc) = 0;
    virtual void completeTrackPluginInsertion(TrackID trackId, uint32_t slotIndex, void* rawInstance, const struct PluginDescriptor& plugDesc) = 0;

    // Sidechain Operations
    virtual bool setTrackSidechainRouting(TrackID targetTrackId, uint32_t slotIndex, TrackID sourceTrackId, float sendGainLinear = 1.0f) = 0;
    virtual void clearTrackSidechainRouting(TrackID targetTrackId, uint32_t slotIndex) = 0;
    virtual bool getTrackSidechainRouting(TrackID targetTrackId, uint32_t slotIndex, TrackID& outSourceTrackId, float& outSendGainLinear) const = 0;
    virtual bool detectFeedbackCycle(TrackID sourceTrackId, TrackID targetTrackId) const = 0;

    // Callback Registration
    virtual void registerMixerRoutingCallback(MixerRoutingCallback cb) = 0;

    /**
     * @brief Recalculate all absolute sample caches in all playlists and MIDI sequencers
     *        from their stored musical BBT positions. Must be called off the RT thread
     *        whenever the TempoMap changes (e.g., after setBPM or addTempoPoint).
     * @param tempoService The tempo service to query for BBT-to-sample conversion.
     */
    virtual void recalculateTimeCaches(Layer2::ITempoService* tempoService) = 0;
};

} // namespace composition
