#pragma once
#include "musical_composition/interfaces/imarker_manager.h"
#include "musical_composition/command_history/icommand_history.h"
#include "musical_composition/command_history/delta_primitives.h"
#include <vector>
#include <mutex>

namespace composition {

class MarkerManagerImpl : public IMarkerManager {
public:
    explicit MarkerManagerImpl(ICommandHistory* history);
    ~MarkerManagerImpl() override = default;

    MarkerUUID addMarker(const MarkerUUID& uuid, uint64_t framePosition, const char* label, uint32_t colorARGB, bool pushDelta = true) override;
    void removeMarker(const MarkerUUID& uuid, bool pushDelta = true) override;
    void updateMarker(const MarkerUUID& uuid, uint64_t framePosition, const char* label, uint32_t colorARGB, bool pushDelta = true) override;
    uint32_t getMarkersInRange(uint64_t startFrame, uint64_t endFrame,
                               MarkerInfo* outMarkers, uint32_t maxCount) const override;
    void clear() override;

    // For Undo/Redo delta application
    void applyDelta(const ProjectDelta& delta, bool isUndo);

    // Direct access for serialization
    const std::vector<MarkerInfo>& getMarkersDirect() const { return markers_; }
    void setMarkersDirect(const std::vector<MarkerInfo>& markers);

    // Helper functions
    MarkerUUID generateUUID() const;
    void sortAndReindexMarkers();

private:
    ICommandHistory* history_;
    std::vector<MarkerInfo> markers_;
    mutable std::mutex mutex_;

    void addMarkerInternal(const MarkerUUID& uuid, uint64_t framePosition, const char* label, uint32_t colorARGB);
    void removeMarkerInternal(const MarkerUUID& uuid);
    void updateMarkerInternal(const MarkerUUID& uuid, uint64_t framePosition, const char* label, uint32_t colorARGB);
};

} // namespace composition
