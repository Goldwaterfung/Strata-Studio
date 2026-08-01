#pragma once

#include "project_state.h"
#include <string>

namespace composition {

struct ProjectJsonSerializer {
    /**
     * @brief Serialize the ProjectState POD tree into a formatted JSON string.
     */
    static bool serialize(
        const ProjectState& state,
        std::string& outJsonString
    );

    /**
     * @brief Deserialize a JSON string into a ProjectState struct.
     */
    static bool deserialize(
        const std::string& jsonString,
        ProjectState& outState
    );
};

} // namespace composition
