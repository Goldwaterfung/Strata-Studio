#pragma once

#include "project_state.h"
#include <vector>
#include <cstdint>

namespace composition {

struct ProjectSerializer {
    /**
     * @brief Serialize the ProjectState POD tree into a binary buffer matching the AGDAW_PROJ_V6 format.
     */
    static bool serialize(
        const ProjectState& state,
        std::vector<uint8_t>& outBuffer
    );

    /**
     * @brief Deserialize a binary buffer into a ProjectState POD tree.
     */
    static bool deserialize(
        const std::vector<uint8_t>& inBuffer,
        ProjectState& outState
    );

private:
    // ---- Write helpers ----
    static void writeBytes(std::vector<uint8_t>& buf, const void* data, size_t n);

    template<typename T>
    static void writeT(std::vector<uint8_t>& buf, T val) {
        writeBytes(buf, &val, sizeof(T));
    }

    static void writeString(std::vector<uint8_t>& buf, const std::string& s);

    // ---- Read helpers ----
    static bool readBytes(const std::vector<uint8_t>& buf, size_t& offset,
                          void* out, size_t n);

    template<typename T>
    static bool readT(const std::vector<uint8_t>& buf, size_t& offset, T& val) {
        return readBytes(buf, offset, &val, sizeof(T));
    }

    static bool readString(const std::vector<uint8_t>& buf, size_t& offset,
                           std::string& out);
};

} // namespace composition
