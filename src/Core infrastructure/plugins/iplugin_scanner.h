// iplugin_scanner.h
// Layer 2: Core Infrastructure Services - Plugin Scanner Interface
//
// Provides crash-isolated plugin scanning for VST3, AU, and CLAP formats.
// Scanner runs in isolated process to prevent plugin crashes from affecting DAW.
//
// Thread-safety:
// - scanPlugin(), scanDirectory(): Non-blocking, spawns background thread
// - Callbacks invoked from scanner thread (NOT RT-safe)
// - getProgress(), isScanning(): Thread-safe (atomic reads)
// - cancelScan(): Thread-safe (atomic flag)

#pragma once

#include "system_primitives.h"
#include <memory>
#include <cstdint>
#include <functional>
#include <vector>

namespace Layer2 {

// =============================================================================
// PLUGIN SCANNER INTERFACE
// =============================================================================

class IPluginScanner {
public:
    // === Nested Types === //

    // Plugin format enumeration (matches PluginFormat in primitives)
    enum class PluginFormat : uint8_t {
        NONE = 0,
        VST3 = 1,
        AU = 2,
        CLAP = 4,
        ALL = 0xFF  // Scan all supported formats
    };

    // Scan result returned via callback
    struct ScanResult {
        bool success;                                           // True if scan succeeded
        PluginDescriptor descriptor;                            // Plugin metadata (if success)
        char errorMessage[256];                                 // Error message (if failure)
        uint32_t errorCode;                                     // Platform-specific error code
        uint64_t scanTimeMs;                                    // Time taken to scan (ms)
        uint8_t format;                                         // Detected format
        uint8_t reserved[3];

        // Clear result to default state
        void clear() {
            success = false;
            std::memset(&descriptor, 0, sizeof(PluginDescriptor));
            std::memset(errorMessage, 0, sizeof(errorMessage));
            errorCode = 0;
            scanTimeMs = 0;
            format = static_cast<uint8_t>(PluginFormat::NONE);
            reserved[0] = reserved[1] = reserved[2] = 0;
        }
    };

    // Scan progress information
    struct ScanProgress {
        uint32_t currentPlugin;                                 // Current plugin index (1-based)
        uint32_t totalPlugins;                                  // Total plugins to scan
        uint32_t completedPlugins;                              // Successfully scanned
        uint32_t failedPlugins;                                 // Failed scans
        char currentPluginName[MAX_PLUGIN_NAME_LENGTH];         // Current plugin name
        float progress;                                         // Progress (0.0 - 1.0)
        bool isScanning;                                        // Currently scanning

        void clear() {
            currentPlugin = 0;
            totalPlugins = 0;
            completedPlugins = 0;
            failedPlugins = 0;
            std::memset(currentPluginName, 0, sizeof(currentPluginName));
            progress = 0.0f;
            isScanning = false;
        }
    };

    // Scan configuration options
    struct ScanConfig {
        bool validateChecksum;                                  // Verify plugin binary checksum
        bool loadCachedMetadata;                                // Use cached results when available
        bool storeCachedMetadata;                               // Store results in cache
        bool disableCacheValidation;                            // For testing purposes
        bool scanSubdirectories;                                // Recursively scan directories
        uint32_t timeoutMs;                                     // Per-plugin timeout (0 = no timeout)
        uint32_t maxConcurrentScans;                            // Max parallel scanners (1 = serial)
        PluginFormat formatsToScan;                             // Which formats to scan

        void setDefaults() {
            validateChecksum = true;
            loadCachedMetadata = true;
            scanSubdirectories = true;
            timeoutMs = 10000;                                  // 10 second default
            maxConcurrentScans = 1;                             // Serial scanning (safe default)
            formatsToScan = PluginFormat::ALL;
        }
    };

    // Callback function pointer type
    // context: User-provided context pointer
    // result: Scan result (caller does NOT own memory)
    using ScanCallback = void(*)(void* context, const ScanResult& result);

    // === Single Plugin Scanning === //

    // Scan a single plugin file
    // Spawns background scanner process, callback invoked when complete
    // Thread-safety: Non-blocking, safe from any thread
    // Parameters:
    //   pluginPath: Absolute path to plugin file/directory
    //   callback: Invoked when scan completes (from scanner thread)
    //   context: User context passed to callback
    virtual void scanPlugin(const char* pluginPath,
                           ScanCallback callback,
                           void* context) = 0;

    // Scan a single plugin with explicit format hint
    virtual void scanPluginWithFormat(const char* pluginPath,
                                     PluginFormat format,
                                     ScanCallback callback,
                                     void* context) = 0;

    // === Directory Scanning === //

    // Scan all plugins in a directory
    // Discovers plugins by extension (.vst3, .component, .clap)
    // Thread-safety: Non-blocking, safe from any thread
    virtual void scanDirectory(const char* directoryPath,
                              ScanCallback callback,
                              void* context) = 0;

    // Scan directory with configuration options
    virtual void scanDirectoryWithConfig(const char* directoryPath,
                                        const ScanConfig& config,
                                        ScanCallback callback,
                                        void* context) = 0;

    // === Scan Control === //

    // Cancel ongoing scan
    // Waits for scanner processes to terminate
    // Thread-safety: Thread-safe, blocks until scanners stopped
    virtual void cancelScan() = 0;

    // Query if scanner is currently active
    // Thread-safety: Thread-safe, non-blocking
    virtual bool isScanning() const = 0;

    // Get current scan progress
    // Thread-safety: Thread-safe, returns snapshot of current state
    virtual ScanProgress getProgress() const = 0;

    // === Cache Management === //

    // Load cached metadata for plugin (if available)
    // Returns: True if cache hit and outDesc populated
    // Thread-safety: Thread-safe
    virtual bool loadCachedMetadata(const char* pluginPath,
                                   PluginDescriptor* outDesc) const = 0;

    // Store plugin metadata in cache
    // Thread-safety: Thread-safe
    virtual void storeCachedMetadata(const char* pluginPath,
                                    const PluginDescriptor& desc) = 0;

    // Invalidate entire plugin cache
    // Thread-safety: Thread-safe
    virtual void invalidateCache() = 0;

    // Save cache to persistent storage
    // Thread-safety: Thread-safe
    virtual bool saveCache() = 0;

    // Load cache from persistent storage
    // Thread-safety: Thread-safe
    virtual bool loadCache() = 0;

    // Get all cached descriptors
    // Thread-safety: Thread-safe
    virtual std::vector<PluginDescriptor> getCachedDescriptors() const = 0;

    // === Configuration === //

    // Set scan configuration
    virtual void setConfig(const ScanConfig& config) = 0;

    // Get current scan configuration
    virtual ScanConfig getConfig() const = 0;

    // === Factory === //

    static std::unique_ptr<IPluginScanner> create(const ScanConfig& config = {});

    virtual ~IPluginScanner() = default;
};

// =============================================================================
// PLUGIN VALIDATOR INTERFACE
// =============================================================================

class IPluginValidator {
public:
    // Validation result
    struct ValidationResult {
        bool isValid;                                          // True if plugin passes validation
        bool hasRequiredExtensions;                            // Required entry points present
        bool matchesFormatSpecification;                       // Format structure valid
        bool isSafeToLoad;                                     // No known malicious signatures
        char validationMessage[256];                           // Human-readable message
        uint32_t formatVersion;                                // Detected format version
        uint32_t reserved;

        void clear() {
            isValid = false;
            hasRequiredExtensions = false;
            matchesFormatSpecification = false;
            isSafeToLoad = false;
            std::memset(validationMessage, 0, sizeof(validationMessage));
            formatVersion = 0;
            reserved = 0;
        }
    };

    // Validate plugin file without loading
    // Returns: Validation result with detailed status
    virtual ValidationResult validatePlugin(const char* pluginPath,
                                          IPluginScanner::PluginFormat format) = 0;

    // Compute plugin checksum for cache validation
    virtual uint32_t computeChecksum(const char* pluginPath) = 0;

    virtual ~IPluginValidator() = default;
};

} // namespace Layer2

// COMPILE-TIME ASSERTION: ScanResult must be POD
static_assert(std::is_pod<Layer2::IPluginScanner::ScanResult>::value,
              "IPluginScanner::ScanResult must be Plain Old Data");

// COMPILE-TIME ASSERTION: ScanProgress must be POD
static_assert(std::is_pod<Layer2::IPluginScanner::ScanProgress>::value,
              "IPluginScanner::ScanProgress must be Plain Old Data");

// COMPILE-TIME ASSERTION: ScanConfig must be POD
static_assert(std::is_pod<Layer2::IPluginScanner::ScanConfig>::value,
              "IPluginScanner::ScanConfig must be Plain Old Data");

// COMPILE-TIME ASSERTION: ValidationResult must be POD
static_assert(std::is_pod<Layer2::IPluginValidator::ValidationResult>::value,
              "IPluginValidator::ValidationResult must be Plain Old Data");
