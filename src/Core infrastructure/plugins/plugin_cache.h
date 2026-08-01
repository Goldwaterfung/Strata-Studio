// plugin_cache.h
// Layer 2: Core Infrastructure Services - Plugin Metadata Cache
//
// Thread-safe cache for plugin metadata with persistence to disk.
// Cache is updated after successful plugin scans and loaded on startup.
//
// Thread-safety: All methods are thread-safe (mutex-protected)

#pragma once

#include "system_primitives.h"
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <cstdint>

namespace Layer2 {

// =============================================================================
// PLUGIN CACHE
// =============================================================================

class PluginCache {
private:
    // Cache entry with metadata and file information
    struct CacheEntry {
        PluginDescriptor descriptor;
        uint64_t cacheTime;                                    // When entry was added
        uint64_t lastValidated;                               // Last validation timestamp
        uint32_t accessCount;                                 // Access frequency tracking
        uint8_t reserved[4];

        CacheEntry() : cacheTime(0), lastValidated(0), accessCount(0) {
            reserved[0] = reserved[1] = reserved[2] = reserved[3] = 0;
        }
    };

    // Cache file header (for persistence)
    struct CacheFileHeader {
        uint32_t magic;                                        // 'PCHE' magic number
        uint32_t version;                                      // Cache format version
        uint64_t timestamp;                                    // When cache was written
        uint32_t entryCount;                                   // Number of entries
        uint32_t checksum;                                     // Header checksum
    };

    // Cache file format constants
    static constexpr uint32_t CACHE_MAGIC = 0x50434845;       // 'PCHE' in ASCII
    static constexpr uint32_t CACHE_VERSION_CURRENT = 3;      // Current cache format version
    static constexpr uint32_t MAX_CACHE_ENTRIES = 10000;      // Safety limit for entry count

    std::unordered_map<std::string, CacheEntry> cache;
    mutable std::mutex mutex;
    std::string cachePath;
    uint64_t maxMemoryBytes;
    uint64_t currentMemoryBytes;
    bool validationEnabled;

    // Platform-specific cache directory
    static std::string getDefaultCachePath();

    // Compute cache key from plugin path
    static std::string getCacheKey(const std::string& pluginPath);

    // Compute checksum for cache file header
    uint32_t computeHeaderChecksum(const CacheFileHeader& header) const;

    // Evict least-recently-used entries if memory limit exceeded
    void evictIfNeeded();

    // Validate cache entry against current file
    bool validateEntry(const CacheEntry& entry, const std::string& pluginPath) const;

public:
    // Constructor with default cache path
    PluginCache();

    // Constructor with custom cache path
    explicit PluginCache(const std::string& cachePath, uint64_t maxMemoryBytes = 10 * 1024 * 1024);

    ~PluginCache() = default;

    // === Cache Operations === //

    // Load plugin metadata from cache
    // Returns: True if cache hit and descriptor populated
    bool load(const std::string& pluginPath, PluginDescriptor* outDesc);

    // Store plugin metadata in cache
    void store(const std::string& pluginPath, const PluginDescriptor& desc);

    // Check if plugin is in cache (without loading)
    bool contains(const std::string& pluginPath) const;

    // Remove entry from cache
    void remove(const std::string& pluginPath);

    // Clear all cache entries
    void clear();

    // === Persistence === //

    // Save cache to disk
    // Returns: True if save succeeded
    bool save();

    // Load cache from disk
    // Returns: True if load succeeded (cache may be partially loaded)
    bool load();

    // Invalidate cache file (delete from disk)
    void invalidate();

    // === Cache Management === //

    // Set maximum memory usage
    void setMaxMemoryBytes(uint64_t maxBytes);

    // Get current memory usage
    uint64_t getCurrentMemoryBytes() const;

    // Get cache entry count
    size_t size() const;

    // Enable/disable file system validation
    void setValidationEnabled(bool enabled);

    // Check if cache is empty
    bool isEmpty() const;

    // === Batch Operations === //

    // Get all cached descriptors
    std::vector<PluginDescriptor> getAllDescriptors() const;

    // Remove invalid entries (files modified/deleted)
    uint32_t pruneInvalidEntries();
};

// =============================================================================
// INLINE IMPLEMENTATIONS
// =============================================================================

inline std::string PluginCache::getCacheKey(const std::string& pluginPath) {
    // Normalize path: convert to absolute, resolve symlinks
    // For now, use absolute path as key
    return pluginPath;
}

inline bool PluginCache::contains(const std::string& pluginPath) const {
    std::lock_guard<std::mutex> lock(mutex);
    return cache.find(getCacheKey(pluginPath)) != cache.end();
}

inline void PluginCache::clear() {
    std::lock_guard<std::mutex> lock(mutex);
    cache.clear();
    currentMemoryBytes = 0;
}

inline void PluginCache::setMaxMemoryBytes(uint64_t maxBytes) {
    std::lock_guard<std::mutex> lock(mutex);
    maxMemoryBytes = maxBytes;
    evictIfNeeded();
}

inline uint64_t PluginCache::getCurrentMemoryBytes() const {
    std::lock_guard<std::mutex> lock(mutex);
    return currentMemoryBytes;
}

inline size_t PluginCache::size() const {
    std::lock_guard<std::mutex> lock(mutex);
    return cache.size();
}

inline bool PluginCache::isEmpty() const {
    std::lock_guard<std::mutex> lock(mutex);
    return cache.empty();
}

} // namespace Layer2
