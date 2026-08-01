#pragma once
#include "project/isession_manager.h"
#include "Core infrastructure/bridges/imutation_bridge.h"
#include "Core infrastructure/memory/istring_registry.h"
#include "telemetry/imetering_provider.h"
#include "automation/iautomation_recording_gateway.h"
#include "engine/ihardware_settings_facade.h"
#include "common/system_primitives.h"
#include "tracks/itrack_controller.h"
#include <mutex>
#include <vector>
#include <unordered_map>
#include <string>
#include <functional>

namespace Layer3 { class IPluginManager; class ITransport; }
namespace DSP   { class LatencyFactory; }
namespace composition { class ITrackManager; }

namespace bridge {
class IRecordingController;

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

struct TrackControllerContext {
    ISessionManager*            sessionManager;
    IRecordingController*       recordingController;
    Layer2::IMutationBridge*    mutationBridge;
    Layer2::IStringRegistry*    stringRegistry;
    IMeteringProvider*          meteringProvider;
    Layer3::IPluginManager*     pluginManager;
    NodeID                      masterChannelStripNode;
    NodeID                      masterBusNode;
    NodeID                      masterPluginSlotNode;
    NodeID                      masterLatencyNode;
    DSP::LatencyFactory*        latencyFactory;
    Layer3::ITransport*         transport;
    IAutomationRecordingGateway* recordingGateway;
    IHardwareSettingsFacade*    hardwareFacade;
    std::recursive_mutex&       mutex;

    std::unordered_map<SendCacheKey, SendSlotCache, SendCacheKeyHash>& sendCache;
    std::unordered_map<uint64_t, std::vector<ParameterDescriptorCacheItem>>& trackParameterCache;
    std::unordered_map<uint64_t, bool>& automationExpanded;
    std::unordered_map<uint64_t, std::vector<bool>>& subLanesExpanded;
    std::unordered_map<uint64_t, std::vector<uint32_t>>& subLaneHeights;
    std::unordered_map<uint64_t, bool>& trackSelection;
    std::unordered_map<uint64_t, LastTweakedParameter>& lastTweakedCache;

    
    
    

    ITrackController* facade = nullptr;

    composition::ITrackManager* getTrackManager() const {
        if (sessionManager) {
            if (auto* session = sessionManager->getActiveSession()) {
                return session->getTrackManager();
            }
        }
        return nullptr;
    }
    
    SendSlotCache& getSendCache(TrackID trackId, bool isPreFader, uint32_t slotIndex) const {
        SendCacheKey key{trackId, isPreFader, slotIndex};
        auto it = sendCache.find(key);
        if (it == sendCache.end()) {
            SendSlotCache cache;
            cache.gainLinear = 0.0f; 
            cache.isEnabled = false;  
            cache.destinationNodeId = NodeID::invalid();
            cache.destinationName = "-- Empty --";
            sendCache[key] = cache;
            return sendCache[key];
        }
        return sendCache[key];
    }
};

} // namespace bridge
