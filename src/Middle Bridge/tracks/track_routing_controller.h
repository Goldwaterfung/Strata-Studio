#pragma once
#include "tracks/track_controller_context.h"
#include "common/system_primitives.h"
#include <unordered_map>
#include <string>

#include "tracks/itrack_controller.h"

namespace bridge {

class TrackRoutingController {
public:
    explicit TrackRoutingController(TrackControllerContext context);

    void setSendGain(TrackID trackId, bool isPreFader, uint32_t sendIndex, float gainLinear);
    void setSendPan(TrackID trackId, bool isPreFader, uint32_t sendIndex, float panPosition);
    void setSendEnabled(TrackID trackId, bool isPreFader, uint32_t sendIndex, bool enabled);
    void setSendDestination(TrackID trackId, bool isPreFader, uint32_t sendIndex, NodeID destinationNodeId);
    void setTrackAudioInput(TrackID trackId, uint32_t mappedPhysicalInputIndex, uint32_t numChannels);
    void setTrackInput(TrackID trackId, uint32_t optionId, uint32_t numChannels);

    // --- Sidechain Routing Controls ---
    void setPluginSidechainSource(TrackID targetTrackId, uint32_t slotIndex, TrackID sourceTrackId, float sendGaindB = 0.0f);
    void clearPluginSidechainSource(TrackID targetTrackId, uint32_t slotIndex);
    [[nodiscard]] std::vector<TrackInputOption> getAvailableSidechainSources(TrackID targetTrackId) const;
    [[nodiscard]] SidechainSlotUIState getPluginSidechainState(TrackID targetTrackId, uint32_t slotIndex) const;


    struct SendCacheKey {
        TrackID trackId;
        bool isPreFader;
        uint32_t slotIndex;
        bool operator==(const SendCacheKey& o) const {
            return trackId == o.trackId && isPreFader == o.isPreFader && slotIndex == o.slotIndex;
        }
    };
    struct SendCacheKeyHash {
        std::size_t operator()(const SendCacheKey& k) const noexcept {
            return std::hash<uint64_t>{}(k.trackId.toRaw()) ^ (std::hash<bool>{}(k.isPreFader) << 1) ^ (std::hash<uint32_t>{}(k.slotIndex) << 2);
        }
    };
    struct SendSlotCache {
        float gainLinear = 0.0f; 
        bool isEnabled = false;  
        NodeID destinationNodeId = NodeID::invalid();
        std::string destinationName = "-- Empty --";
    };

    SendSlotCache& getSendCache(TrackID trackId, bool isPreFader, uint32_t slotIndex);

private:
    TrackControllerContext ctx_;
    std::unordered_map<SendCacheKey, SendSlotCache, SendCacheKeyHash> sendCache_;
};

} // namespace bridge
