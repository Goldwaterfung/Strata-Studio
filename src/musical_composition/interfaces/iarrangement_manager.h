#pragma once
#include "common/system_primitives.h"
#include <vector>

namespace composition {

/**
 * @brief Layer 5 interface for managing project arrangements/scenes.
 */
class IArrangementManager {
public:
    virtual ~IArrangementManager() = default;

    virtual std::vector<ArrangementInfo> getArrangements() const = 0;
    virtual ArrangementID getActiveArrangement() const = 0;
    virtual void setActiveArrangement(ArrangementID id) = 0;

    virtual ArrangementID createArrangement(const char* name) = 0;
    virtual void renameArrangement(ArrangementID id, const char* newName) = 0;
    virtual void deleteArrangement(ArrangementID id) = 0;
    virtual ArrangementID cloneArrangement(ArrangementID id, const char* cloneName) = 0;
    
    virtual void mergeArrangements(
        ArrangementID sourceId,
        ArrangementID destId,
        int mergeMode,
        const MergeFilterOptions& filterOptions
    ) = 0;
};

} // namespace composition
