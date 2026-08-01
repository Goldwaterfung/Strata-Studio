// plugin_scanner.cpp
// Layer 2: Core Infrastructure Services - Plugin Scanner Implementation
//
// Main implementation of the plugin scanner with process isolation,
// caching, and format detection.

#include "iplugin_scanner.h"
#include <project_config.h>
#include "plugin_cache.h"
#include "scanner_process.h"
#include "plugin_validator.h"
#include "Hardware/OS abstraction/threading/ithread_manager.h"
#include "Hardware/OS abstraction/filesystem/ifile_system.h"
#include "Hardware/OS abstraction/common/platform_detection.h"
#include <algorithm>
#include <cstring>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <queue>
#include <memory>
#include <iostream>

// Portability ensured via Layer 1 IFileSystem

namespace Layer2 {

// =============================================================================
// SCAN QUEUE ITEM
// =============================================================================

struct ScanQueueItem {
    std::string pluginPath;
    IPluginScanner::PluginFormat format;
    IPluginScanner::ScanCallback callback;
    void* context;
};

// =============================================================================
// PLUGIN SCANNER IMPLEMENTATION
// =============================================================================

class PluginScannerImpl : public IPluginScanner {
private:
    // Thread management
    std::unique_ptr<Layer1::IThreadManager> threadManager;
    Layer1::ThreadHandle scannerThread;
    std::atomic<bool> shouldStop;

    // Scan queue
    std::queue<ScanQueueItem> scanQueue;
    std::mutex queueMutex;
    std::condition_variable queueCV;

    // Scan state
    std::atomic<bool> scanningActive;
    ScanProgress currentProgress;
    mutable std::mutex progressMutex;

    // Cache and validator
    std::unique_ptr<PluginCache> cache;
    std::unique_ptr<PluginValidator> validator;

    // Process pool
    std::unique_ptr<ScannerProcessManager> processPool;

    // Configuration
    ScanConfig config;

    // File System abstraction
    std::unique_ptr<Layer1::IFileSystem> fileSystem;

    // Statistics
    struct {
        std::atomic<uint64_t> totalScans;
        std::atomic<uint64_t> cacheHits;
        std::atomic<uint64_t> cacheMisses;
        std::atomic<uint64_t> failedScans;
    } stats;

    // === Scanner Thread Function === //

    // Wrapper struct for passing raw pointer through ThreadContext
    struct ScannerThreadContext {
        PluginScannerImpl* scanner;
    };

    static void scannerThreadFunction(void* context) {
        auto* ctx = static_cast<ScannerThreadContext*>(context);
        auto* scanner = ctx->scanner;

        while (!scanner->shouldStop.load(std::memory_order_acquire)) {
            ScanQueueItem item;

            // Wait for work
            {
                std::unique_lock<std::mutex> lock(scanner->queueMutex);

                scanner->queueCV.wait(lock, [&] {
                    return scanner->shouldStop.load(std::memory_order_acquire) ||
                           !scanner->scanQueue.empty();
                });

                if (scanner->shouldStop.load(std::memory_order_acquire)) {
                    break;
                }

                if (scanner->scanQueue.empty()) {
                    continue;
                }

                item = scanner->scanQueue.front();
                scanner->scanQueue.pop();
            }

            // Process scan
            scanner->processScan(item);
        }
    }

    // === Scan Processing === //

    void processScan(const ScanQueueItem& item) {
        ScanResult result = {};
        result.clear();
        result.format = static_cast<uint8_t>(item.format);

        auto startTime = std::chrono::steady_clock::now();

        // Check cache first
        if (config.loadCachedMetadata) {
            PluginDescriptor cachedDesc = {};
            if (cache->load(item.pluginPath, &cachedDesc)) {
                // Verify cache entry is still valid
                IPluginValidator::ValidationResult validation = validator->validatePlugin(
                    item.pluginPath.c_str(), item.format);

                if (validation.isValid) {
                    result.success = true;
                    result.descriptor = cachedDesc;
                    stats.cacheHits.fetch_add(1, std::memory_order_relaxed);
                } else {
                    // Cache entry stale, remove it
                    cache->remove(item.pluginPath);
                    stats.cacheMisses.fetch_add(1, std::memory_order_relaxed);
                }
            } else {
                stats.cacheMisses.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // If cache miss, perform actual scan
        if (!result.success) {
            result = performScan(item.pluginPath, item.format);
        }

        auto endTime = std::chrono::steady_clock::now();
        result.scanTimeMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count());

        // Update statistics
        stats.totalScans.fetch_add(1, std::memory_order_relaxed);

        if (!result.success) {
            stats.failedScans.fetch_add(1, std::memory_order_relaxed);
        } else {
            // Store successful scan in cache
            cache->store(item.pluginPath, result.descriptor);
        }

        // Update progress
        updateProgress(item.pluginPath, result.success);

        // Invoke callback
        if (item.callback) {
            item.callback(item.context, result);
        }
    }

    ScanResult performScan(const std::string& pluginPath, IPluginScanner::PluginFormat format) {
        ScanResult result = {};
        result.clear();
        result.format = static_cast<uint8_t>(format);

        // Validate plugin first
        IPluginValidator::ValidationResult validation = validator->validatePlugin(
            pluginPath.c_str(), format);

        if (!validation.isValid) {
            result.success = false;
            std::strncpy(result.errorMessage, validation.validationMessage,
                        sizeof(result.errorMessage) - 1);
            return result;
        }

        // Acquire scanner process
        ScannerProcess* process = processPool->acquire(pluginPath.c_str(), format);

        if (!process) {
            result.success = false;
            std::strncpy(result.errorMessage, "Failed to acquire scanner process",
                        sizeof(result.errorMessage) - 1);
            return result;
        }

        // Wait for scan to complete
        result = process->wait(config.timeoutMs);

        // Release process
        processPool->release(process);

        // Fallback to in-process scanning if process-isolated scan failed or wasn't supported
        if (!result.success) {
            PluginDescriptor desc = {};
            if (validator->extractBasicMetadata(pluginPath.c_str(), format, desc)) {
                result.success = true;
                result.descriptor = desc;
                std::memset(result.errorMessage, 0, sizeof(result.errorMessage));
            }
        }

        return result;
    }

    void updateProgress(const std::string& pluginPath, bool success) {
        std::lock_guard<std::mutex> lock(progressMutex);

        // Extract plugin name for display
        size_t lastSlash = pluginPath.find_last_of("/\\");
        std::string name = (lastSlash != std::string::npos)
            ? pluginPath.substr(lastSlash + 1)
            : pluginPath;

        std::strncpy(currentProgress.currentPluginName, name.c_str(),
                    sizeof(currentProgress.currentPluginName) - 1);

        currentProgress.currentPlugin++;

        if (success) {
            currentProgress.completedPlugins++;
        } else {
            currentProgress.failedPlugins++;
        }

        if (currentProgress.totalPlugins > 0) {
            currentProgress.progress =
                static_cast<float>(currentProgress.currentPlugin) /
                static_cast<float>(currentProgress.totalPlugins);
            
            if (currentProgress.currentPlugin >= currentProgress.totalPlugins) {
                currentProgress.isScanning = false;
            }
        }
    }

    // === Directory Scanning === //

    // Plugin bundle information
    struct BundleInfo {
        bool isBundle;              // True if path is a bundle directory
        bool isValidBundle;         // True if bundle structure is valid
        char binaryPath[MAX_PATH_LENGTH];  // Path to actual binary inside bundle

        BundleInfo() : isBundle(false), isValidBundle(false) {
            binaryPath[0] = '\0';
        }
    };

    // Check if filename is a plugin file by extension
    static bool isPluginFile(const char* filename) {
        if (!filename) {
            return false;
        }

        std::string name(filename);
        for (char &c : name) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        size_t len = name.length();

        // Check for .vst3 (5 chars)
        if (len >= 5 && name.compare(len - 5, 5, ".vst3") == 0) {
            return true;
        }

        // Check for .clap (5 chars)
        if (len >= 5 && name.compare(len - 5, 5, ".clap") == 0) {
            return true;
        }

        // Check for .component (10 chars)
        if (len >= 10 && name.compare(len - 10, 10, ".component") == 0) {
            return true;
        }

        return false;
    }

    // Detect if path is a plugin bundle and extract binary path
    BundleInfo detectBundle(const char* path) {
        BundleInfo info;
        if (!path) {
            return info;
        }

        std::string pathStr(path);

        // Check if it ends with plugin extension (case-insensitive)
        std::string lowerPath = pathStr;
        for (char &c : lowerPath) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }

        bool hasPluginExtension = false;
        if (lowerPath.length() >= 5) {
            std::string ext = lowerPath.substr(lowerPath.length() - 5);
            if (ext == ".vst3" || ext == ".clap") {
                hasPluginExtension = true;
            }
        }

        if (lowerPath.length() >= 10) {
            std::string ext = lowerPath.substr(lowerPath.length() - 10);
            if (ext == ".component") {
                hasPluginExtension = true;
            }
        }

        if (!hasPluginExtension) {
            return info;
        }

        info.isBundle = true;

        Layer1::FileInfo st;
        if (!fileSystem->getPathInfo(path, st)) {
            return info;
        }

#if defined(LAYER1_PLATFORM_MACOS)
        // macOS bundle structure: BundleName.extension/Contents/MacOS/BundleName
        std::string contentsPath = pathStr + "/Contents/MacOS";

        Layer1::FileInfo contentsInfo;
        if (fileSystem->getPathInfo(contentsPath.c_str(), contentsInfo) && contentsInfo.isDirectory) {
            // Find binary inside
            size_t lastSlash = pathStr.find_last_of("/\\");
            std::string binaryName = (lastSlash != std::string::npos)
                ? pathStr.substr(lastSlash + 1)
                : pathStr;

            // Remove extension
            size_t dotPos = binaryName.find_last_of('.');
            if (dotPos != std::string::npos) {
                binaryName = binaryName.substr(0, dotPos);
            }

            std::string binaryPath = contentsPath + "/" + binaryName;

            Layer1::FileInfo binaryInfo;
            if (fileSystem->getPathInfo(binaryPath.c_str(), binaryInfo) && !binaryInfo.isDirectory) {
                info.isValidBundle = true;
                std::strncpy(info.binaryPath, binaryPath.c_str(), MAX_PATH_LENGTH - 1);
                info.binaryPath[MAX_PATH_LENGTH - 1] = '\0';
            } else {
                // Fallback: search Contents/MacOS/ for the first executable binary
                fileSystem->iterateDirectory(contentsPath.c_str(), [&](const Layer1::FileInfo& subInfo) {
                    if (info.isValidBundle) return;
                    if (!subInfo.isDirectory && std::strcmp(subInfo.name, ".") != 0 &&
                        std::strcmp(subInfo.name, "..") != 0 && !subInfo.isHidden) {
                        std::string subBinaryPath = contentsPath + "/" + subInfo.name;
                        info.isValidBundle = true;
                        std::strncpy(info.binaryPath, subBinaryPath.c_str(), MAX_PATH_LENGTH - 1);
                        info.binaryPath[MAX_PATH_LENGTH - 1] = '\0';
                    }
                });
            }
        }

#elif defined(LAYER1_PLATFORM_WINDOWS)
        // Windows VST3 can be single file or bundle
        if (st.isDirectory) {
            // Bundle - look for binary inside
            // Windows VST3 bundle: PluginName.vst3/x86_64-win/PluginName.vst3
            size_t lastSlash = pathStr.find_last_of("/\\");
            std::string binaryName = (lastSlash != std::string::npos)
                ? pathStr.substr(lastSlash + 1)
                : pathStr;

            std::string binaryPath = pathStr + "/x86_64-win/" + binaryName;

            Layer1::FileInfo binaryInfo;
            if (fileSystem->getPathInfo(binaryPath.c_str(), binaryInfo) && !binaryInfo.isDirectory) {
                info.isValidBundle = true;
                std::strncpy(info.binaryPath, binaryPath.c_str(), MAX_PATH_LENGTH - 1);
                info.binaryPath[MAX_PATH_LENGTH - 1] = '\0';
            }
        } else {
            // Single file - use as-is
            info.isBundle = false;
        }

#endif

        return info;
    }

    std::vector<std::string> discoverPlugins(const std::string& directoryPath) {
        std::vector<std::string> pluginPaths;

        fileSystem->iterateDirectory(directoryPath.c_str(), [&](const Layer1::FileInfo& info) {
            // Skip . and .. handled by iterateDirectory (usually) but let's be safe
            if (std::strcmp(info.name, ".") == 0 || std::strcmp(info.name, "..") == 0) {
                return;
            }

            // Skip hidden files
            if (info.isHidden) {
                return;
            }

            std::string fullPath = directoryPath;
#if defined(LAYER1_PLATFORM_WINDOWS)
            if (!fullPath.empty() && fullPath.back() != '\\' && fullPath.back() != '/') {
                fullPath += '\\';
            }
#else
            if (!fullPath.empty() && fullPath.back() != '/') {
                fullPath += '/';
            }
#endif
            fullPath += info.name;

            if (info.isDirectory) {
                // Check if it's a plugin bundle
                if (isPluginFile(info.name)) {
                    BundleInfo bundleInfo = detectBundle(fullPath.c_str());

                    if (bundleInfo.isBundle) {
                        pluginPaths.push_back(fullPath);
                    } else if (config.scanSubdirectories) {
                        // Regular directory - recurse
                        auto subPlugins = discoverPlugins(fullPath);
                        pluginPaths.insert(pluginPaths.end(),
                                         subPlugins.begin(), subPlugins.end());
                    }
                } else if (config.scanSubdirectories) {
                    // Not a plugin bundle, recurse into directory
                    auto subPlugins = discoverPlugins(fullPath);
                    pluginPaths.insert(pluginPaths.end(),
                                     subPlugins.begin(), subPlugins.end());
                }
            } else {
                // Regular file - check if it's a plugin
                if (isPluginFile(info.name)) {
                    pluginPaths.push_back(fullPath);
                }
            }
        });

        return pluginPaths;
    }

public:
    PluginScannerImpl(const IPluginScanner::ScanConfig& cfg) : config(cfg) {
        fileSystem = Layer1::IFileSystem::create();
        threadManager = Layer1::IThreadManager::create();
        shouldStop.store(false, std::memory_order_release);
        scanningActive.store(false, std::memory_order_release);

        cache = std::make_unique<PluginCache>();
        cache->setValidationEnabled(!config.disableCacheValidation);
        validator = std::make_unique<PluginValidator>();
        processPool = std::make_unique<ScannerProcessManager>(1);



        currentProgress.clear();

        // Reset statistics
        stats.totalScans.store(0, std::memory_order_relaxed);
        stats.cacheHits.store(0, std::memory_order_relaxed);
        stats.cacheMisses.store(0, std::memory_order_relaxed);
        stats.failedScans.store(0, std::memory_order_relaxed);

        // Start scanner thread
        ScannerThreadContext ctx{this};
        Layer1::IThreadManager::ThreadCreateInfo<ScannerThreadContext> threadInfo = {
            .function = scannerThreadFunction,
            .context = Layer1::IThreadManager::ThreadContext<ScannerThreadContext>(ctx),
            .name = "PluginScanner",
            .priority = Layer1::ThreadPriority::LOW,  // Background task
            .rtConstraints = {},
            .stackSize = 0,
            .preferredCore = UINT32_MAX
        };

        scannerThread = threadManager->createThread(threadInfo);
    }

    ~PluginScannerImpl() override {
        // Stop scanner thread
        shouldStop.store(true, std::memory_order_release);
        queueCV.notify_all();

        // Clean up queue
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            while (!scanQueue.empty()) {
                scanQueue.pop();
            }
        }

        // Thread will be cleaned up by ThreadManager
    }

    // === IPluginScanner Implementation === //

    void scanPlugin(const char* pluginPath,
                   ScanCallback callback,
                   void* context) override
    {
        // Detect format
        IPluginScanner::PluginFormat format = PluginValidator::detectFormat(pluginPath);

        if (format == IPluginScanner::PluginFormat::NONE) {
            // Unknown format, return error immediately
            ScanResult result = {};
            result.clear();
            result.success = false;
            std::strncpy(result.errorMessage, "Unknown plugin format",
                        sizeof(result.errorMessage) - 1);

            if (callback) {
                callback(context, result);
            }
            return;
        }

        scanPluginWithFormat(pluginPath, format, callback, context);
    }

    void scanPluginWithFormat(const char* pluginPath,
                             IPluginScanner::PluginFormat format,
                             ScanCallback callback,
                             void* context) override
    {
        ScanQueueItem item = {};
        item.pluginPath = pluginPath;
        item.format = format;
        item.callback = callback;
        item.context = context;

        {
            std::lock_guard<std::mutex> lock(queueMutex);

            // Initialize progress for single scan
            std::lock_guard<std::mutex> progressLock(progressMutex);
            currentProgress.totalPlugins = 1;
            currentProgress.currentPlugin = 0;
            currentProgress.isScanning = true;

            scanQueue.push(item);
        }

        queueCV.notify_one();
    }

    void scanDirectory(const char* directoryPath,
                      ScanCallback callback,
                      void* context) override
    {
        ScanConfig defaultConfig;
        defaultConfig.setDefaults();
        scanDirectoryWithConfig(directoryPath, defaultConfig, callback, context);
    }

    void scanDirectoryWithConfig(const char* directoryPath,
                                const ScanConfig& scanConfig,
                                ScanCallback callback,
                                void* context) override
    {
        config = scanConfig;

        // Discover plugins in directory
        std::vector<std::string> pluginPaths = discoverPlugins(directoryPath);

        if (pluginPaths.empty()) {
            // No plugins found, notify immediately
            ScanResult result = {};
            result.clear();
            result.success = false;
            std::strncpy(result.errorMessage, "No plugins found in directory",
                        sizeof(result.errorMessage) - 1);

            if (callback) {
                callback(context, result);
            }
            return;
        }

        // Initialize progress
        {
            std::lock_guard<std::mutex> lock(progressMutex);
            currentProgress.totalPlugins = static_cast<uint32_t>(pluginPaths.size());
            currentProgress.currentPlugin = 0;
            currentProgress.completedPlugins = 0;
            currentProgress.failedPlugins = 0;
            currentProgress.progress = 0.0f;
            currentProgress.isScanning = true;
        }

        // Queue all plugins for scanning
        for (const auto& path : pluginPaths) {
            ScanQueueItem item = {};
            item.pluginPath = path;
            item.format = PluginValidator::detectFormat(path.c_str());
            item.callback = callback;
            item.context = context;

            {
                std::lock_guard<std::mutex> lock(queueMutex);
                scanQueue.push(item);
            }
        }

        queueCV.notify_all();
    }

    void cancelScan() override {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            while (!scanQueue.empty()) {
                scanQueue.pop();
            }
        }

        processPool->cancelAll();

        std::lock_guard<std::mutex> lock(progressMutex);
        currentProgress.isScanning = false;
    }

    bool isScanning() const override {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(queueMutex));
        return !scanQueue.empty() || currentProgress.isScanning;
    }

    ScanProgress getProgress() const override {
        std::lock_guard<std::mutex> lock(progressMutex);
        return currentProgress;
    }

    bool loadCachedMetadata(const char* pluginPath,
                           PluginDescriptor* outDesc) const override {
        return cache->load(pluginPath, outDesc);
    }

    void storeCachedMetadata(const char* pluginPath,
                            const PluginDescriptor& desc) override {
        cache->store(pluginPath, desc);
    }

    void invalidateCache() override {
        cache->clear();
    }

    bool saveCache() override {
        return cache->save();
    }

    bool loadCache() override {
        return cache->load();
    }

    std::vector<PluginDescriptor> getCachedDescriptors() const override {
        return cache->getAllDescriptors();
    }

    void setConfig(const ScanConfig& newConfig) override {
        config = newConfig;
        processPool->setMaxConcurrentScans(config.maxConcurrentScans);
    }

    ScanConfig getConfig() const override {
        return config;
    }
};

// =============================================================================
// PLUGIN CACHE IMPLEMENTATION
// =============================================================================

PluginCache::PluginCache()
    : maxMemoryBytes(10 * 1024 * 1024)  // 10 MB default
    , currentMemoryBytes(0)
    , validationEnabled(true)
{
    cachePath = getDefaultCachePath();
    load();
}

PluginCache::PluginCache(const std::string& cachePath, uint64_t maxMemoryBytes)
    : cachePath(cachePath)
    , maxMemoryBytes(maxMemoryBytes)
    , currentMemoryBytes(0)
    , validationEnabled(true)
{
    load();
}

bool PluginCache::load(const std::string& pluginPath, PluginDescriptor* outDesc) {
    std::lock_guard<std::mutex> lock(mutex);

    auto it = cache.find(getCacheKey(pluginPath));

    if (it == cache.end()) {
        return false;
    }

    // Validate entry against current file
    if (!validateEntry(it->second, pluginPath)) {
        cache.erase(it);
        return false;
    }

    if (outDesc) {
        *outDesc = it->second.descriptor;
        it->second.accessCount++;
    }

    return true;
}

void PluginCache::store(const std::string& pluginPath, const PluginDescriptor& desc) {
    std::lock_guard<std::mutex> lock(mutex);

    auto key = getCacheKey(pluginPath);
    auto it = cache.find(key);

    if (it != cache.end()) {
        // Update existing entry
        currentMemoryBytes -= sizeof(it->second.descriptor);
        it->second.descriptor = desc;
        it->second.lastValidated = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    } else {
        // Add new entry
        CacheEntry entry;
        entry.descriptor = desc;
        entry.cacheTime = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        entry.lastValidated = entry.cacheTime;
        entry.accessCount = 1;

        cache[key] = entry;
    }

    currentMemoryBytes += sizeof(desc);
    evictIfNeeded();
}

void PluginCache::remove(const std::string& pluginPath) {
    std::lock_guard<std::mutex> lock(mutex);

    auto it = cache.find(getCacheKey(pluginPath));

    if (it != cache.end()) {
        currentMemoryBytes -= sizeof(it->second.descriptor);
        cache.erase(it);
    }
}

bool PluginCache::save() {
    std::lock_guard<std::mutex> lock(mutex);

    auto fs = Layer1::IFileSystem::create();

    // Open file for writing
    auto handle = fs->openFile(cachePath.c_str(), false);  // false = forWriting

    if (handle == Layer1::INVALID_FILE_HANDLE) {
        return false;
    }

    // Prepare header
    CacheFileHeader header = {};
    header.magic = CACHE_MAGIC;
    header.version = CACHE_VERSION_CURRENT;
    header.timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    header.entryCount = static_cast<uint32_t>(cache.size());
    header.checksum = 0;  // Will compute below

    // Compute header checksum
    header.checksum = computeHeaderChecksum(header);

    // Write header
    uint8_t headerBuffer[sizeof(CacheFileHeader)];
    std::memcpy(headerBuffer, &header, sizeof(CacheFileHeader));

    uint64_t bytesWritten = fs->writeFileSync(handle, 0, headerBuffer, sizeof(CacheFileHeader));

    if (bytesWritten != sizeof(CacheFileHeader)) {
        fs->closeFile(handle);
        return false;
    }

    // Write all plugin descriptors
    uint64_t offset = sizeof(CacheFileHeader);

    for (const auto& entry : cache) {
        const PluginDescriptor& desc = entry.second.descriptor;

        uint8_t descBuffer[sizeof(PluginDescriptor)];
        std::memcpy(descBuffer, &desc, sizeof(PluginDescriptor));

        bytesWritten = fs->writeFileSync(handle, offset, descBuffer, sizeof(PluginDescriptor));

        if (bytesWritten != sizeof(PluginDescriptor)) {
            fs->closeFile(handle);
            return false;
        }

        offset += sizeof(PluginDescriptor);
    }

    // Close file
    fs->closeFile(handle);

    return true;
}

bool PluginCache::load() {
    std::lock_guard<std::mutex> lock(mutex);

    auto fs = Layer1::IFileSystem::create();

    // Open file for reading
    auto handle = fs->openFile(cachePath.c_str(), true);  // true = forReading

    if (handle == Layer1::INVALID_FILE_HANDLE) {
        // File doesn't exist - not an error, just empty cache
        return true;
    }

    // Read header
    uint8_t headerBuffer[sizeof(CacheFileHeader)];
    uint64_t bytesRead = fs->readFileSync(handle, 0, headerBuffer, sizeof(CacheFileHeader));

    if (bytesRead != sizeof(CacheFileHeader)) {
        fs->closeFile(handle);
        return false;
    }

    CacheFileHeader header;
    std::memcpy(&header, headerBuffer, sizeof(CacheFileHeader));

    // Validate magic number
    if (header.magic != CACHE_MAGIC) {
        fs->closeFile(handle);
        invalidate();
        return false;
    }

    // Validate version
    if (header.version > CACHE_VERSION_CURRENT) {
        // Cache file is from future version - can't read it
        fs->closeFile(handle);
        invalidate();
        return false;
    }

    if (header.version < CACHE_VERSION_CURRENT) {
        // Old version - discard and rebuild
        fs->closeFile(handle);
        invalidate();
        return true;  // Not an error, just empty cache
    }

    // Validate checksum
    uint32_t expectedChecksum = header.checksum;
    header.checksum = 0;  // Clear for validation
    uint32_t computedChecksum = computeHeaderChecksum(header);

    if (expectedChecksum != computedChecksum) {
        fs->closeFile(handle);
        invalidate();
        return false;  // Corrupt header
    }

    // Validate entry count
    if (header.entryCount > MAX_CACHE_ENTRIES) {
        fs->closeFile(handle);
        invalidate();
        return false;  // Suspicious entry count
    }

    // Read all plugin descriptors
    uint64_t offset = sizeof(CacheFileHeader);

    for (uint32_t i = 0; i < header.entryCount; ++i) {
        uint8_t descBuffer[sizeof(PluginDescriptor)];
        bytesRead = fs->readFileSync(handle, offset, descBuffer, sizeof(PluginDescriptor));

        if (bytesRead != sizeof(PluginDescriptor)) {
            // Partial read - stop here
            break;
        }

        PluginDescriptor desc;
        std::memcpy(&desc, descBuffer, sizeof(PluginDescriptor));

        // Validate descriptor has valid path
        if (desc.filePath[0] == '\0') {
            offset += sizeof(PluginDescriptor);
            continue;  // Skip invalid entries
        }

        // Add to cache
        std::string pluginPath(desc.filePath);
        CacheEntry entry;
        entry.descriptor = desc;
        entry.cacheTime = header.timestamp;
        entry.lastValidated = 0;
        entry.accessCount = 0;

        cache[getCacheKey(pluginPath)] = entry;
        currentMemoryBytes += sizeof(PluginDescriptor);

        offset += sizeof(PluginDescriptor);
    }

    fs->closeFile(handle);

    // Validate all entries against current files
    pruneInvalidEntries();

    return true;
}

void PluginCache::invalidate() {
    std::lock_guard<std::mutex> lock(mutex);
    cache.clear();
    currentMemoryBytes = 0;
}

std::vector<PluginDescriptor> PluginCache::getAllDescriptors() const {
    std::lock_guard<std::mutex> lock(mutex);

    std::vector<PluginDescriptor> descriptors;
    descriptors.reserve(cache.size());

    for (const auto& entry : cache) {
        descriptors.push_back(entry.second.descriptor);
    }

    return descriptors;
}

uint32_t PluginCache::pruneInvalidEntries() {
    std::lock_guard<std::mutex> lock(mutex);

    uint32_t pruned = 0;

    for (auto it = cache.begin(); it != cache.end(); ) {
        if (!validateEntry(it->second, it->first)) {
            currentMemoryBytes -= sizeof(it->second.descriptor);
            it = cache.erase(it);
            pruned++;
        } else {
            ++it;
        }
    }

    return pruned;
}

std::string PluginCache::getDefaultCachePath() {
    const std::string projName(config::PROJECT_NAME);
#if defined(LAYER1_PLATFORM_MACOS)
    return std::string(getenv("HOME")) + "/Library/Caches/" + projName + "/plugins.cache";
#elif defined(LAYER1_PLATFORM_WINDOWS)
    return std::string(getenv("APPDATA")) + "/" + projName + "/plugins.cache";
#else
    return "/tmp/" + projName + "/plugins.cache";
#endif
}

uint32_t PluginCache::computeHeaderChecksum(const CacheFileHeader& header) const {
    uint32_t checksum = 0;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(&header);
    constexpr size_t CHECKSUM_OFFSET = offsetof(CacheFileHeader, checksum);

    for (size_t i = 0; i < CHECKSUM_OFFSET; ++i) {
        checksum = (checksum << 1) | (checksum >> 31);
        checksum += data[i];
    }

    return checksum;
}

void PluginCache::evictIfNeeded() {
    while (currentMemoryBytes > maxMemoryBytes && !cache.empty()) {
        // Find least recently used entry
        auto lruIt = cache.begin();

        for (auto it = cache.begin(); it != cache.end(); ++it) {
            if (it->second.lastValidated < lruIt->second.lastValidated ||
                (it->second.lastValidated == lruIt->second.lastValidated &&
                 it->second.accessCount < lruIt->second.accessCount)) {
                lruIt = it;
            }
        }

        currentMemoryBytes -= sizeof(lruIt->second.descriptor);
        cache.erase(lruIt);
    }
}

void PluginCache::setValidationEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    validationEnabled = enabled;
}

bool PluginCache::validateEntry(const CacheEntry& entry, const std::string& pluginPath) const {
    if (!validationEnabled) {
        return true;
    }

    // Check if file still exists
    auto fs = Layer1::IFileSystem::create();
    Layer1::FileInfo info;

    if (!fs->getPathInfo(pluginPath.c_str(), info)) {
        return false;  // File doesn't exist
    }

    // Check modification time
    if (info.lastModified != entry.descriptor.fileModTime) {
        return false;  // File has been modified
    }

    // Check file size
    if (info.size != entry.descriptor.fileSize) {
        return false;  // File size changed
    }

    return true;  // Entry is valid
}

// =============================================================================
// FACTORY FUNCTION
// =============================================================================

std::unique_ptr<IPluginScanner> IPluginScanner::create(const ScanConfig& config) {
    ScanConfig actualConfig = config;
    
    // If config appears to be default-initialized (zeroed), apply defaults
    // Check timeoutMs as a heuristic (defaults to 5000)
    if (actualConfig.timeoutMs == 0 && actualConfig.maxConcurrentScans == 0) {
        actualConfig.setDefaults();
    }
    
    return std::make_unique<PluginScannerImpl>(actualConfig);
}

} // namespace Layer2
