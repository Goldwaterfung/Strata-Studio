#include "tracks/itrack_controller.h"
#pragma once
#include "tracks/track_controller_context.h"
#include "common/system_primitives.h"

namespace bridge {

class TrackPluginController {
public:
    explicit TrackPluginController(TrackControllerContext context);

    void insertInstrument(TrackID trackId, uint32_t pluginId);
    void removeInstrument(TrackID trackId);
    void setInstrumentBypassed(TrackID trackId, bool bypassed);
    bool openInstrumentEditor(TrackID trackId, void* parentWindow, int& outWidth, int& outHeight);
    void closeInstrumentEditor(TrackID trackId);

    void insertPlugin(TrackID trackId, uint32_t slotIndex, uint32_t pluginId);
    void removePlugin(TrackID trackId, uint32_t slotIndex);
    void setPluginBypassed(TrackID trackId, uint32_t slotIndex, bool bypassed);
    std::vector<uint8_t> getPluginState(TrackID trackId, uint32_t slotIndex) const;
    void setPluginState(TrackID trackId, uint32_t slotIndex, const std::vector<uint8_t>& state);
    void setPluginParameter(TrackID trackId, uint32_t slotIndex, uint32_t paramIndex, float value);
    uint32_t getPluginIdInSlot(TrackID trackId, uint32_t slotIndex) const;
    uint32_t findPluginIdByName(std::string_view name) const;
    bool openPluginEditor(TrackID trackId, uint32_t slotIndex, void* parentWindow, int& outWidth, int& outHeight);
    void closePluginEditor(TrackID trackId, uint32_t slotIndex);

    void subscribeToPluginParameterTweaks(TrackID trackId, uint32_t slotIndex, bool isInstrument, ITrackController::PluginParameterTweakedCallback cb);
    void unsubscribeFromPluginParameterTweaks(TrackID trackId, uint32_t slotIndex, bool isInstrument);

    void completeInstrumentInsertion(TrackID trackId, void* rawInstance, const PluginDescriptor& plugDesc);

private:
    TrackControllerContext ctx_;
};

} // namespace bridge
