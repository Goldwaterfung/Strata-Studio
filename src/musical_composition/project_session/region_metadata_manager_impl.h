#pragma once
#include "musical_composition/interfaces/iregion_metadata_manager.h"
#include "musical_composition/command_history/icommand_history.h"
#include "musical_composition/command_history/delta_primitives.h"
#include <unordered_map>
#include <mutex>

namespace composition {

class RegionMetadataManagerImpl : public IRegionMetadataManager {
public:
    explicit RegionMetadataManagerImpl(ICommandHistory* history);
    ~RegionMetadataManagerImpl() override = default;

    void setRegionMetadata(RegionID id, const RegionMetadata& metadata, bool pushDelta = true) override;
    void getRegionMetadata(RegionID id, RegionMetadata& outMetadata) const override;
    void removeRegionMetadata(RegionID id, bool pushDelta = true) override;
    bool hasRegionMetadata(RegionID id) const override;
    
    void clear() override;

    // Undo/Redo application
    void applyDelta(const ProjectDelta& delta, bool isUndo);

    // Direct access for ProjectState extraction/restoration
    const std::unordered_map<RegionID, RegionMetadata>& getMetadataDirect() const { return metadataMap_; }
    void setMetadataDirect(const std::unordered_map<RegionID, RegionMetadata>& map);

private:
    ICommandHistory* history_;
    std::unordered_map<RegionID, RegionMetadata> metadataMap_;
    mutable std::mutex mutex_; // Mutex protects map against concurrent access during background project serialization
};

} // namespace composition
