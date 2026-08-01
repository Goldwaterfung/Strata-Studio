#pragma once

#include "project_state.h"
#include "iproject_session.h"
#include "musical_composition/track_manager/itrack_manager.h"
#include <string>
#include <vector>

namespace Layer2 { class IStringRegistry; }
namespace Layer3 { class IPluginManager; }

namespace composition {

class ProjectStateBridge {
public:
    /**
     * @brief Extracts the full C++ project state from the live session and track manager.
     */
    static ProjectState extract(
        const IProjectSession& session,
        ITrackManager* trackManager,
        Layer2::IStringRegistry* stringRegistry,
        const std::string& projectFilePath
    );

    /**
     * @brief Restores the full project state back into the live session and track manager.
     */
    static bool restore(
        const ProjectState& state,
        IProjectSession& outSession,
        ITrackManager* trackManager,
        Layer2::IStringRegistry* stringRegistry,
        Layer3::IPluginManager* pluginManager,
        std::vector<MissingPluginReport>* outMissingPlugins
    );
};

} // namespace composition
