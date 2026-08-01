#pragma once
#include "common/system_primitives.h"
#include <vector>

namespace bridge {

/**
 * @brief Controller interface for managing active arrangement scenes and scene configurations.
 * Exposes queries and mutations for CRUD operations and cross-arrangement merges.
 */
class IArrangementManagerController {
public:
    virtual ~IArrangementManagerController() = default;

    // Queries
    virtual std::vector<ArrangementInfo> getArrangements() const = 0;
    virtual ArrangementID getActiveArrangement() const = 0;
    virtual void setActiveArrangement(ArrangementID id) = 0;

    // Mutations
    virtual ArrangementID createArrangement(const char* name) = 0;
    virtual void renameArrangement(ArrangementID id, const char* newName) = 0;
    virtual void deleteArrangement(ArrangementID id) = 0;
    virtual ArrangementID cloneArrangement(ArrangementID id, const char* cloneName) = 0;
    
    // Cross-Arrangement Operations
    virtual void mergeArrangements(
        ArrangementID sourceId,
        ArrangementID destId,
        int mergeMode,
        const MergeFilterOptions& filterOptions
    ) = 0;
};

} // namespace bridge
