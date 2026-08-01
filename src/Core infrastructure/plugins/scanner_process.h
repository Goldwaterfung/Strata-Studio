// scanner_process.h
// Layer 2: Core Infrastructure Services - Plugin Scanner Process Isolation
//
// Provides crash-isolated plugin scanning by spawning child processes.
// If a plugin crashes during scanning, only the child process dies.
//
// Platform-specific implementation:
// - Unix/macOS: fork() + pipe() for IPC
// - Windows: CreateProcess() + anonymous pipes

#pragma once

#include "iplugin_scanner.h"
#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#endif

namespace Layer2 {

// =============================================================================
// SCANNER PROCESS - Cross-Platform Process Isolation
// =============================================================================

class ScannerProcess {
private:
    // Platform-specific process handles
#ifdef PLATFORM_WINDOWS
    HANDLE childProcess;
    HANDLE readPipe;
    HANDLE writePipe;
#else
    pid_t childPid;
    int readPipe;                                            // Parent reads from child
    int writePipe;                                           // Parent writes to child
#endif

    std::atomic<bool> isRunning;
    std::atomic<uint32_t> exitCode;

    // Communication protocol
    struct MessageHeader {
        uint32_t magic;                                       // 'SCAN' magic
        uint32_t messageType;                                 // Request/Response
        uint32_t payloadSize;                                 // Size of following data
        uint32_t checksum;                                    // Header checksum
    };

    enum MessageType : uint32_t {
        SCAN_REQUEST = 1,
        SCAN_RESPONSE = 2,
        CANCEL_REQUEST = 3,
        ERROR_RESPONSE = 4
    };

    // Create pipes for IPC
    bool createPipes();

    // Close pipes
    void closePipes();

    // Send message to child process
    bool sendMessage(MessageType type, const void* payload, uint32_t size);

    // Receive message from child process
    bool receiveMessage(MessageType* outType, std::vector<uint8_t>& buffer);

    // Read exact number of bytes (handles partial reads)
    bool readExact(void* buffer, size_t size);

    // Write exact number of bytes (handles partial writes)
    bool writeExact(const void* buffer, size_t size);

public:
    ScannerProcess();
    ~ScannerProcess();

    // === Process Lifecycle === //

    // Spawn scanner process for plugin
    // Returns: True if process spawned successfully
    bool spawn(const char* pluginPath, IPluginScanner::PluginFormat format);

    // Wait for scan to complete
    // Returns: Scan result (valid if success)
    IPluginScanner::ScanResult wait(uint32_t timeoutMs = 5000);

    // Terminate scanner process (force kill)
    void terminate();

    // Check if process is still running
    bool isProcessRunning() const;

    // Internal check for process death
    void checkProcessStatus();

    // Get process exit code
    uint32_t getExitCode() const;

    // === Communication === //

    // Check if child has responded
    bool hasResponse() const;

    // === Platform-Specific === //

#ifndef PLATFORM_WINDOWS
    // POSIX-specific helpers
    static bool setNonBlocking(int fd);
    static bool setCloseOnExec(int fd);
#endif
};

// =============================================================================
// SCANNER PROCESS MANAGER - Pool of Scanner Processes
// =============================================================================

class ScannerProcessManager {
private:
    struct ScannerSlot {
        std::unique_ptr<ScannerProcess> process;
        std::string pluginPath;
        IPluginScanner::PluginFormat format;
        bool inUse;

        ScannerSlot() : format(IPluginScanner::PluginFormat::NONE), inUse(false) {}
    };

    std::vector<ScannerSlot> pool;
    uint32_t maxConcurrentScans;
    std::atomic<uint32_t> activeScans;
    mutable std::mutex poolMutex;

public:
    explicit ScannerProcessManager(uint32_t maxConcurrent = 1);
    ~ScannerProcessManager();

    // Acquire scanner process from pool
    // Returns: Scanner process pointer or nullptr if pool full
    ScannerProcess* acquire(const char* pluginPath,
                           IPluginScanner::PluginFormat format);

    // Release scanner process back to pool
    void release(ScannerProcess* process, bool keepProcess = false);

    // Cancel all active scans
    void cancelAll();

    // Get number of active scans
    uint32_t getActiveScanCount() const;

    // Check if pool is full
    bool isFull() const;

    // Set maximum concurrent scans
    void setMaxConcurrentScans(uint32_t max);

private:
    // Find available slot in pool
    ScannerSlot* findAvailableSlot();

    // Clean up terminated processes
    void cleanupTerminated();
};

// =============================================================================
// INLINE IMPLEMENTATIONS
// =============================================================================

inline bool ScannerProcess::isProcessRunning() const {
    const_cast<ScannerProcess*>(this)->checkProcessStatus();
    return isRunning.load(std::memory_order_acquire);
}

inline uint32_t ScannerProcess::getExitCode() const {
    return exitCode.load(std::memory_order_acquire);
}

inline uint32_t ScannerProcessManager::getActiveScanCount() const {
    return activeScans.load(std::memory_order_acquire);
}

inline bool ScannerProcessManager::isFull() const {
    return getActiveScanCount() >= maxConcurrentScans;
}

inline void ScannerProcessManager::setMaxConcurrentScans(uint32_t max) {
    std::lock_guard<std::mutex> lock(poolMutex);
    maxConcurrentScans = max;
    if (pool.size() < maxConcurrentScans) {
        pool.resize(maxConcurrentScans);
    }
}

} // namespace Layer2
