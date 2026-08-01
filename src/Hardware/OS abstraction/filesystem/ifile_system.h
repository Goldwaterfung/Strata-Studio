// ifile_system.h
// Layer 1: Hardware/OS Abstraction - File System Abstraction Interface
// PURE INTERFACE: No platform-specific headers allowed

#pragma once

#include <cstdint>
#include <memory>
#include <functional>
#include "../common/layer1_primitives.h"

namespace Layer1 {

// =============================================================================
// FILE SYSTEM INTERFACE
// =============================================================================

class IFileSystem {
public:
    // === Nested Types === //

    // Async callback interface (implemented by Layer 6)
    class IAsyncCallback {
    public:
        virtual ~IAsyncCallback() = default;

        // Success callback: Takes ownership of data buffer
        // CRITICAL: Caller MUST delete[] the data pointer (not free())
        // This is because Layer 1 allocates with new[] for C++ compatibility
        virtual void onSuccess(uint8_t* data, uint64_t bytesProcessed) = 0;

        // Error callback: No ownership transfer
        virtual void onError(int errorCode, const char* errorMessage) = 0;
    };

    // === File Lifecycle === //

    // Open file for reading or writing
    // Returns: Valid handle on success, INVALID_FILE_HANDLE on failure
    // Thread-safe: Can be called from any thread
    [[nodiscard]] virtual FileHandle openFile(const char* path, bool forReading = true) = 0;

    // Close file handle
    // Precondition: handle must be valid
    // Postcondition: Handle is invalidated, resources released
    // Thread-safe: Can be called from any thread
    virtual void closeFile(FileHandle handle) = 0;

    // Get file size in bytes
    // Returns: File size in bytes, 0 on error
    // Thread-safe: Can be called from any thread
    virtual uint64_t getFileSize(FileHandle handle) = 0;

    // === Async Read Operations === //

    // Initiate async read operation (buffer allocated by filesystem)
    // handle: Valid file handle from openFile()
    // offset: Byte offset in file (must be aligned to page size for optimal performance)
    // bytesToRead: Number of bytes to read
    // callback: Invoked when operation completes (success or failure)
    //
    // BUFFER OWNERSHIP:
    // - Caller provides NO buffer (filesystem allocates)
    // - Filesystem owns buffer during operation
    // - onSuccess callback receives ownership (must delete[])
    // - onError callback: no ownership transfer (filesystem frees)
    // - If canceled: filesystem frees buffer
    //
    // Returns: Operation handle for cancellation/status tracking
    [[nodiscard]] virtual OperationHandle readFileAsync(FileHandle handle,
                                         uint64_t offset,
                                         uint64_t bytesToRead,
                                         IAsyncCallback* callback) = 0;

    // === Async Write Operations === //

    // Initiate async write operation (caller provides buffer)
    // handle: Valid file handle from openFile()
    // offset: Byte offset in file
    // data: Buffer to write (filesystem takes ownership, will delete[])
    // bytesToWrite: Number of bytes to write
    // callback: Invoked when operation completes
    //
    // BUFFER OWNERSHIP:
    // - Caller allocates buffer with new[]
    // - Filesystem takes ownership immediately (will delete[])
    // - If canceled: filesystem frees buffer
    //
    // Returns: Operation handle for cancellation
    [[nodiscard]] virtual OperationHandle writeFileAsync(FileHandle handle,
                                          uint64_t offset,
                                          uint8_t* data,  // Filesystem takes ownership
                                          uint64_t bytesToWrite,
                                          IAsyncCallback* callback) = 0;

    // === Operation Control === //

    // Cancel pending async operation
    // If operation completes before cancellation, callback is invoked normally
    // If canceled successfully, callback is NOT invoked, buffers are freed
    // Returns: true if cancellation succeeded
    [[nodiscard]] virtual bool cancelOperation(OperationHandle op) = 0;

    // === Priority Control === //

    // Set I/O priority for file handle
    // Maps to OS I/O priority (Windows I/O priority, Linux ionice)
    // Returns: true if priority set successfully
    [[nodiscard]] virtual bool setPriority(FileHandle handle, IOPriority priority) = 0;

    // === Sync Operations === //

    // Synchronous read (caller provides buffer)
    // Returns: Number of bytes read (0 on error or EOF)
    // Thread-safety: Blocks caller until complete (use from worker thread only)
    virtual uint64_t readFileSync(FileHandle handle,
                                 uint64_t offset,
                                 uint8_t* buffer,  // Caller-owned
                                 uint64_t bytesToRead) = 0;

    // Synchronous write (caller provides buffer)
    // Returns: Number of bytes written (0 on error)
    // Thread-safety: Blocks caller until complete (use from worker thread only)
    virtual uint64_t writeFileSync(FileHandle handle,
                                  uint64_t offset,
                                  const uint8_t* buffer,
                                  uint64_t bytesToWrite) = 0;
    
    // === Directory & Metadata Operations === //
    
    // Iterate through directory contents
    // callback is called for each entry found
    // Returns: true if directory exists and iteration completed
    virtual bool iterateDirectory(const char* path, 
                                 const std::function<void(const FileInfo&)>& callback) = 0;

    // Get metadata for a specific path without opening it
    // Returns: true if path exists and info was populated
    virtual bool getPathInfo(const char* path, FileInfo& outInfo) = 0;

    // Check if a path exists
    virtual bool exists(const char* path) = 0;

    // === Factory === //

    static std::unique_ptr<IFileSystem> create();

    virtual ~IFileSystem() = default;
};

} // namespace Layer1
