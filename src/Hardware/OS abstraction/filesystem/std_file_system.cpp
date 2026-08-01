// std_file_system.cpp
// Layer 1: Hardware/OS Abstraction - Standard Library File System Implementation

#include "ifile_system.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <map>
#include <mutex>
#include <atomic>
#include <vector>
#include <deque>
#include <condition_variable>
#include <functional>
#include <memory>
#include <cstring>
#include <sys/stat.h>

#include "file_buffer_pool.h"
#include "filesystem_defaults.h"

#if defined(__APPLE__)
#include "workers/macos_io_worker.cpp"
#elif defined(_WIN32)
#include "workers/windows_io_worker.cpp"
#endif

namespace Layer1 {

namespace fs = std::filesystem;

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

struct FileHandleState {
#if defined(_WIN32)
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int fd = -1;
#endif
    IOPriority priority = IOPriority::NORMAL;
    bool isReading = true;
};

struct AsyncTask {
    OperationHandle handle;
    std::function<void()> work;
    bool cancelled = false;
};

class StdFileSystem : public IFileSystem {
public:
    StdFileSystem(size_t numThreads = FilesystemDefaults::DEFAULT_WORKER_THREADS) 
        : shutdownRequested(false) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this] { workerThread(); });
        }
    }

    ~StdFileSystem() {
        {
            std::lock_guard<std::mutex> lock(tasksMutex);
            shutdownRequested = true;
        }
        tasksCV.notify_all();
        for (auto& worker : workers) {
            if (worker.joinable()) worker.join();
        }

        std::lock_guard<std::mutex> lock(filesMutex);
        for (auto& pair : files) {
#if defined(_WIN32)
            if (pair.second->handle != INVALID_HANDLE_VALUE) CloseHandle(pair.second->handle);
#else
            if (pair.second->fd != -1) close(pair.second->fd);
#endif
        }
    }

    FileHandle openFile(const char* path, bool forReading) override {
        std::lock_guard<std::mutex> lock(filesMutex);
        
        auto info = std::make_unique<FileHandleState>();
        info->isReading = forReading;

#if defined(_WIN32)
        info->handle = CreateFileA(path, 
                                  forReading ? GENERIC_READ : GENERIC_WRITE,
                                  FILE_SHARE_READ, 
                                  NULL, 
                                  forReading ? OPEN_EXISTING : CREATE_ALWAYS, 
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, 
                                  NULL);
        if (info->handle == INVALID_HANDLE_VALUE) return INVALID_FILE_HANDLE;
#else
        int flags = forReading ? O_RDONLY : (O_WRONLY | O_CREAT | O_TRUNC);
        info->fd = open(path, flags, FilesystemDefaults::DEFAULT_FILE_PERMISSIONS);
        if (info->fd == -1) return INVALID_FILE_HANDLE;
#endif

        FileHandle handle = nextFileHandle++;
        files[handle] = std::move(info);
        return handle;
    }

    void closeFile(FileHandle handle) override {
        std::lock_guard<std::mutex> lock(filesMutex);
        auto it = files.find(handle);
        if (it != files.end()) {
#if defined(_WIN32)
            if (it->second->handle != INVALID_HANDLE_VALUE) CloseHandle(it->second->handle);
#else
            if (it->second->fd != -1) close(it->second->fd);
#endif
            files.erase(it);
        }
    }

    uint64_t getFileSize(FileHandle handle) override {
#if defined(_WIN32)
        HANDLE winHandle = INVALID_HANDLE_VALUE;
#else
        int fd = -1;
#endif

        {
            std::lock_guard<std::mutex> lock(filesMutex);
            auto it = files.find(handle);
            if (it == files.end()) return 0;
#if defined(_WIN32)
            winHandle = it->second->handle;
            if (winHandle == INVALID_HANDLE_VALUE) return 0;
#else
            fd = it->second->fd;
            if (fd == -1) return 0;
#endif
        }

#if defined(_WIN32)
        LARGE_INTEGER size;
        if (GetFileSizeEx(winHandle, &size)) {
            return static_cast<uint64_t>(size.QuadPart);
        }
        return 0;
#else
        struct stat st;
        if (fstat(fd, &st) == 0) {
            return static_cast<uint64_t>(st.st_size);
        }
        return 0;
#endif
    }

    OperationHandle readFileAsync(FileHandle handle,
                                 uint64_t offset,
                                 uint64_t bytesToRead,
                                 IAsyncCallback* callback) override {
        OperationHandle op = nextOpHandle++;
        
        enqueueTask(op, [this, handle, offset, bytesToRead, callback]() {
            uint8_t* buffer = FileBufferPool::getInstance().acquire(bytesToRead);
            uint64_t read = readFileSync(handle, offset, buffer, bytesToRead);
            
            if (read > 0) {
                try {
                    callback->onSuccess(buffer, read);
                } catch (...) {
                    FileBufferPool::getInstance().release(buffer, bytesToRead);
                    throw;
                }
            } else {
                FileBufferPool::getInstance().release(buffer, bytesToRead);
                callback->onError(FilesystemDefaults::ERROR_CODE_GENERIC, FilesystemDefaults::MSG_READ_FAILED);
            }
        });

        return op;
    }

    OperationHandle writeFileAsync(FileHandle handle,
                                  uint64_t offset,
                                  uint8_t* data,
                                  uint64_t bytesToWrite,
                                  IAsyncCallback* callback) override {
        OperationHandle op = nextOpHandle++;
        
        enqueueTask(op, [this, handle, offset, data, bytesToWrite, callback]() {
            uint64_t written = writeFileSync(handle, offset, data, bytesToWrite);
            
            if (written == bytesToWrite && written > 0) {
                callback->onSuccess(nullptr, written);
            } else {
                callback->onError(FilesystemDefaults::ERROR_CODE_GENERIC, FilesystemDefaults::MSG_WRITE_FAILED);
            }
            FileBufferPool::getInstance().release(data, bytesToWrite);
        });

        return op;
    }

    bool cancelOperation(OperationHandle op) override {
        std::lock_guard<std::mutex> lock(tasksMutex);
        for (auto& task : tasks) {
            if (task->handle == op) {
                task->cancelled = true;
                return true;
            }
        }
        return false;
    }

    bool setPriority(FileHandle handle, IOPriority priority) override {
        std::lock_guard<std::mutex> lock(filesMutex);
        auto it = files.find(handle);
        if (it != files.end()) {
            it->second->priority = priority;
            return true;
        }
        return false;
    }

    uint64_t readFileSync(FileHandle handle,
                         uint64_t offset,
                         uint8_t* buffer,
                         uint64_t bytesToRead) override {
#if defined(_WIN32)
        HANDLE winHandle = INVALID_HANDLE_VALUE;
#else
        int fd = -1;
#endif
        IOPriority priority = IOPriority::NORMAL;

        {
            std::lock_guard<std::mutex> lock(filesMutex);
            auto it = files.find(handle);
            if (it == files.end()) return 0;
#if defined(_WIN32)
            winHandle = it->second->handle;
            priority = it->second->priority;
            if (winHandle == INVALID_HANDLE_VALUE) return 0;
#else
            fd = it->second->fd;
            priority = it->second->priority;
            if (fd == -1) return 0;
#endif
        }

        // Apply priority on current thread (worker thread)
#if defined(__APPLE__)
        MacOSIOWorker::setIOPriority(fd, priority);
#elif defined(_WIN32)
        WindowsIOWorker::setIOPriority(winHandle, priority);
#endif

#if defined(_WIN32)
        OVERLAPPED ov = {0};
        ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
        ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
        DWORD bytesRead = 0;
        if (!ReadFile(winHandle, buffer, static_cast<DWORD>(bytesToRead), &bytesRead, &ov)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                GetOverlappedResult(winHandle, &ov, &bytesRead, TRUE);
            } else {
                return 0;
            }
        }
        return static_cast<uint64_t>(bytesRead);
#else
        ssize_t res = pread(fd, buffer, static_cast<size_t>(bytesToRead), static_cast<off_t>(offset));
        return res > 0 ? static_cast<uint64_t>(res) : 0;
#endif
    }

    uint64_t writeFileSync(FileHandle handle,
                          uint64_t offset,
                          const uint8_t* buffer,
                          uint64_t bytesToWrite) override {
#if defined(_WIN32)
        HANDLE winHandle = INVALID_HANDLE_VALUE;
#else
        int fd = -1;
#endif
        IOPriority priority = IOPriority::NORMAL;

        {
            std::lock_guard<std::mutex> lock(filesMutex);
            auto it = files.find(handle);
            if (it == files.end()) return 0;
#if defined(_WIN32)
            winHandle = it->second->handle;
            priority = it->second->priority;
            if (winHandle == INVALID_HANDLE_VALUE) return 0;
#else
            fd = it->second->fd;
            priority = it->second->priority;
            if (fd == -1) return 0;
#endif
        }

        // Apply priority on current thread (worker thread)
#if defined(__APPLE__)
        MacOSIOWorker::setIOPriority(fd, priority);
#elif defined(_WIN32)
        WindowsIOWorker::setIOPriority(winHandle, priority);
#endif

#if defined(_WIN32)
        OVERLAPPED ov = {0};
        ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
        ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
        DWORD bytesWritten = 0;
        if (!WriteFile(winHandle, buffer, static_cast<DWORD>(bytesToWrite), &bytesWritten, &ov)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                GetOverlappedResult(winHandle, &ov, &bytesWritten, TRUE);
            } else {
                return 0;
            }
        }
        return static_cast<uint64_t>(bytesWritten);
#else
        ssize_t res = pwrite(fd, buffer, static_cast<size_t>(bytesToWrite), static_cast<off_t>(offset));
        return res > 0 ? static_cast<uint64_t>(res) : 0;
#endif
    }

    bool iterateDirectory(const char* path, 
                         const std::function<void(const FileInfo&)>& callback) override {
        try {
            fs::path p(path);
            if (!fs::exists(p) || !fs::is_directory(p)) return false;

            for (const auto& entry : fs::directory_iterator(p)) {
                FileInfo info = {};
                
                // Copy name
                std::string filename = entry.path().filename().string();
                std::strncpy(info.name, filename.c_str(), MAX_NAME_LENGTH - 1);
                
                // Extension
                std::string ext = entry.path().extension().string();
                std::strncpy(info.extension, ext.c_str(), 15);
                
                info.isDirectory = entry.is_directory();
                
                if (!info.isDirectory) {
                    info.size = fs::file_size(entry.path());
                } else {
                    info.size = 0;
                }
                
                // Last modified
                auto ftime = fs::last_write_time(entry.path());
                info.lastModified = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                    ftime.time_since_epoch()).count());
                
                // Simple checks for common systems
                info.isReadOnly = (fs::status(entry.path()).permissions() & fs::perms::owner_write) == fs::perms::none;
                info.isHidden = filename.size() > 0 && filename[0] == '.';

                callback(info);
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    bool getPathInfo(const char* path, FileInfo& info) override {
        try {
            fs::path p(path);
            if (!fs::exists(p)) return false;

            std::memset(&info, 0, sizeof(FileInfo));
            
            std::string filename = p.filename().string();
            std::strncpy(info.name, filename.c_str(), MAX_NAME_LENGTH - 1);
            
            std::string ext = p.extension().string();
            std::strncpy(info.extension, ext.c_str(), 15);
            
            info.isDirectory = fs::is_directory(p);
            
            if (!info.isDirectory) {
                info.size = fs::file_size(p);
            }
            
            auto ftime = fs::last_write_time(p);
            info.lastModified = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                ftime.time_since_epoch()).count());
            
            info.isReadOnly = (fs::status(p).permissions() & fs::perms::owner_write) == fs::perms::none;
            info.isHidden = filename.size() > 0 && filename[0] == '.';

            return true;
        } catch (...) {
            return false;
        }
    }

    bool exists(const char* path) override {
        try {
            return fs::exists(fs::path(path));
        } catch (...) {
            return false;
        }
    }

private:
    void enqueueTask(OperationHandle handle, std::function<void()> work) {
        {
            std::lock_guard<std::mutex> lock(tasksMutex);
            auto task = std::make_shared<AsyncTask>();
            task->handle = handle;
            task->work = std::move(work);
            tasks.push_back(task);
        }
        tasksCV.notify_one();
    }

    void workerThread() {
        while (true) {
            std::shared_ptr<AsyncTask> task;
            {
                std::unique_lock<std::mutex> lock(tasksMutex);
                tasksCV.wait(lock, [this] { return shutdownRequested || !tasks.empty(); });
                
                if (shutdownRequested && tasks.empty()) return;
                
                if (!tasks.empty()) {
                    task = std::move(tasks.front());
                    tasks.pop_front();
                }
            }
            
            if (task && !task->cancelled) {
                task->work();
            }
        }
    }

    std::map<FileHandle, std::unique_ptr<FileHandleState>> files;
    std::mutex filesMutex;
    std::atomic<uint64_t> nextFileHandle{1};
    std::atomic<uint64_t> nextOpHandle{1};

    std::vector<std::thread> workers;
    std::deque<std::shared_ptr<AsyncTask>> tasks;
    std::mutex tasksMutex;
    std::condition_variable tasksCV;
    std::atomic<bool> shutdownRequested;
};

} // namespace Layer1
