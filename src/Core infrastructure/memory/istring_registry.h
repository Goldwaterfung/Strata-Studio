#pragma once

#include <cstdint>
#include <string>

namespace Layer2 {

/**
 * @brief Thread-safe registry for deduplicating and managing strings via numeric handles.
 * 
 * Used for filenames, paths, track names, etc., to keep POD structures small (uint32_t instead of std::string).
 */
class IStringRegistry {
public:
    virtual ~IStringRegistry() = default;

    /**
     * @brief Register a string and get its unique ID.
     * If the string is already registered, returns the existing ID.
     */
    virtual uint32_t registerString(const std::string& str) = 0;

    /**
     * @brief Resolve a string ID back to its string value.
     * @return true if ID is valid and found.
     */
    virtual bool getString(uint32_t id, std::string& outStr) const = 0;

    /**
     * @brief Remove a string from the registry if it's no longer needed.
     * Note: In many systems, IDs are permanent to avoid stale handle issues.
     */
    virtual void unregisterString(uint32_t id) = 0;

    /**
     * @brief Factory method.
     */
    static std::unique_ptr<IStringRegistry> create();
};

} // namespace Layer2
