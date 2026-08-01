// scanner_process.cpp
// Layer 2: Core Infrastructure Services - Plugin Scanner Process Isolation Implementation
//
// Platform-specific implementation of crash-isolated plugin scanning

#include "scanner_process.h"
#include <cstring>
#include <chrono>
#include <thread>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#else
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Layer2 {

// =============================================================================
// CONSTANTS
// =============================================================================

constexpr uint32_t SCAN_MAGIC = 0x5343414E;  // 'SCAN'

// =============================================================================
// SCANNER PROCESS IMPLEMENTATION
// =============================================================================

ScannerProcess::ScannerProcess()
#ifdef PLATFORM_WINDOWS
    : childProcess(INVALID_HANDLE_VALUE)
    , readPipe(INVALID_HANDLE_VALUE)
    , writePipe(INVALID_HANDLE_VALUE)
#else
    : childPid(-1)
    , readPipe(-1)
    , writePipe(-1)
#endif
    , isRunning(false)
    , exitCode(0)
{
}

ScannerProcess::~ScannerProcess() {
    terminate();
    closePipes();
}

bool ScannerProcess::createPipes() {
#ifdef PLATFORM_WINDOWS
    // Windows: Create anonymous pipes
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;

    HANDLE readHandle, writeHandle;

    // Pipe for child -> parent communication
    if (!CreatePipe(&readPipe, &writeHandle, &sa, 0)) {
        return false;
    }

    // Ensure read handle is not inherited
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    // Pipe for parent -> child communication
    HANDLE parentRead, childWrite;
    if (!CreatePipe(&parentRead, &writePipe, &sa, 0)) {
        CloseHandle(readPipe);
        CloseHandle(writeHandle);
        return false;
    }

    // Ensure write handle is not inherited
    SetHandleInformation(writePipe, HANDLE_FLAG_INHERIT, 0);

    // Close unused handles
    CloseHandle(writeHandle);
    CloseHandle(parentRead);

    return true;

#else
    // Unix/macOS: Create pipe() for bidirectional communication
    int pipes[2];
    if (pipe(pipes) == -1) {
        return false;
    }

    readPipe = pipes[0];
    writePipe = pipes[1];

    // Set close-on-exec flag
    if (!setCloseOnExec(readPipe) || !setCloseOnExec(writePipe)) {
        closePipes();
        return false;
    }

    // Set non-blocking mode
    if (!setNonBlocking(readPipe)) {
        closePipes();
        return false;
    }

    return true;
#endif
}

void ScannerProcess::closePipes() {
#ifdef PLATFORM_WINDOWS
    if (readPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(readPipe);
        readPipe = INVALID_HANDLE_VALUE;
    }
    if (writePipe != INVALID_HANDLE_VALUE) {
        CloseHandle(writePipe);
        writePipe = INVALID_HANDLE_VALUE;
    }
#else
    if (readPipe != -1) {
        close(readPipe);
        readPipe = -1;
    }
    if (writePipe != -1) {
        close(writePipe);
        writePipe = -1;
    }
#endif
}

bool ScannerProcess::spawn(const char* pluginPath, IPluginScanner::PluginFormat format) {
    // Suppress unused parameter warnings (stub implementation)
    (void)pluginPath;
    (void)format;

    // Close any existing process
    terminate();
    closePipes();

    // Create IPC pipes
    if (!createPipes()) {
        return false;
    }

#ifdef PLATFORM_WINDOWS
    // Windows: Create process using Win32 API
    STARTUPINFOA si = {};
    si.cb = sizeof(STARTUPINFOA);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi = {};
    std::string commandLine = "plugin_scanner_child.exe \"" + std::string(pluginPath) + "\"";

    if (!CreateProcessA(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
                       0, nullptr, nullptr, &si, &pi)) {
        closePipes();
        return false;
    }

    CloseHandle(pi.hThread);
    childProcess = pi.hProcess;
    isRunning.store(true, std::memory_order_release);
    return true;

#else
    // Unix/macOS: fork() + exec()
    childPid = fork();

    if (childPid == -1) {
        // Fork failed
        closePipes();
        return false;
    }

    if (childPid == 0) {
        // Child process: exec scanner
        // TODO: Implement child process execution
        // For now, exit with error
        _exit(1);
    }

    // Parent process: continue
    isRunning.store(true, std::memory_order_release);
    return true;
#endif
}

void ScannerProcess::checkProcessStatus() {
    if (!isRunning.load(std::memory_order_acquire)) {
        return;
    }

#ifdef PLATFORM_WINDOWS
    if (childProcess != INVALID_HANDLE_VALUE) {
        DWORD code;
        if (GetExitCodeProcess(childProcess, &code) && code != STILL_ACTIVE) {
            exitCode.store(code, std::memory_order_release);
            isRunning.store(false, std::memory_order_release);
        }
    }
#else
    if (childPid != -1) {
        int status;
        pid_t result = waitpid(childPid, &status, WNOHANG);
        if (result == childPid) {
            if (WIFEXITED(status)) {
                exitCode.store(static_cast<uint32_t>(WEXITSTATUS(status)), std::memory_order_release);
            } else if (WIFSIGNALED(status)) {
                exitCode.store(static_cast<uint32_t>(128 + WTERMSIG(status)), std::memory_order_release);
            }
            isRunning.store(false, std::memory_order_release);
            childPid = -1;
        } else if (result == -1 && errno == ECHILD) {
            isRunning.store(false, std::memory_order_release);
            childPid = -1;
        }
    }
#endif
}

IPluginScanner::ScanResult ScannerProcess::wait(uint32_t timeoutMs) {
    IPluginScanner::ScanResult result = {};
    result.clear();

    if (!isProcessRunning()) {
        result.success = false;
        std::strncpy(result.errorMessage, "Process not running", sizeof(result.errorMessage));
        return result;
    }

    // Wait for response with timeout
    auto startTime = std::chrono::steady_clock::now();
    std::vector<uint8_t> buffer;

    while (true) {
        if (hasResponse()) {
            MessageType type;
            if (receiveMessage(&type, buffer)) {
                if (type == SCAN_RESPONSE) {
                    // Parse response
                    if (buffer.size() >= sizeof(IPluginScanner::ScanResult)) {
                        std::memcpy(&result, buffer.data(), sizeof(IPluginScanner::ScanResult));
                    }
                } else if (type == ERROR_RESPONSE) {
                    result.success = false;
                    std::strncpy(result.errorMessage,
                                reinterpret_cast<const char*>(buffer.data()),
                                std::min(buffer.size(), sizeof(result.errorMessage) - 1));
                }
                break;
            } else {
                // Pipe error or EOF - process probably crashed
                break;
            }
        }

        if (!isProcessRunning()) {
            break;
        }

        // Check timeout
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();

        if (timeoutMs > 0 && static_cast<uint32_t>(elapsed) >= timeoutMs) {
            // Timeout: terminate process
            terminate();
            result.success = false;
            std::strncpy(result.errorMessage, "Scan timeout", sizeof(result.errorMessage));
            result.errorCode = static_cast<uint32_t>(-1);
            break;
        }

        // Small sleep to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Clean up
#ifdef PLATFORM_WINDOWS
    if (childProcess != INVALID_HANDLE_VALUE) {
        CloseHandle(childProcess);
        childProcess = INVALID_HANDLE_VALUE;
    }
#else
    if (childPid != -1) {
        int status;
        waitpid(childPid, &status, 0);
        childPid = -1;
    }
#endif

    isRunning.store(false, std::memory_order_release);
    return result;
}

void ScannerProcess::terminate() {
    if (!isProcessRunning()) {
        return;
    }

#ifdef PLATFORM_WINDOWS
    if (childProcess != INVALID_HANDLE_VALUE) {
        TerminateProcess(childProcess, 1);
        CloseHandle(childProcess);
        childProcess = INVALID_HANDLE_VALUE;
    }
#else
    if (childPid != -1) {
        // Send SIGTERM first
        kill(childPid, SIGTERM);

        // Wait a bit for graceful shutdown
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Force kill if still running
        kill(childPid, SIGKILL);

        int status;
        waitpid(childPid, &status, 0);
        childPid = -1;
    }
#endif

    isRunning.store(false, std::memory_order_release);
}

bool ScannerProcess::sendMessage(MessageType type, const void* payload, uint32_t size) {
    MessageHeader header = {};
    header.magic = SCAN_MAGIC;
    header.messageType = static_cast<uint32_t>(type);
    header.payloadSize = size;
    header.checksum = 0;  // TODO: Compute checksum

    // Write header
    if (!writeExact(&header, sizeof(header))) {
        return false;
    }

    // Write payload
    if (size > 0 && !writeExact(payload, size)) {
        return false;
    }

    return true;
}

bool ScannerProcess::receiveMessage(MessageType* outType, std::vector<uint8_t>& buffer) {
    MessageHeader header;

    // Read header
    if (!readExact(&header, sizeof(header))) {
        return false;
    }

    // Verify magic
    if (header.magic != SCAN_MAGIC) {
        return false;
    }

    // Read payload
    if (header.payloadSize > 0) {
        buffer.resize(header.payloadSize);
        if (!readExact(buffer.data(), header.payloadSize)) {
            return false;
        }
    } else {
        buffer.clear();
    }

    if (outType) {
        *outType = static_cast<MessageType>(header.messageType);
    }

    return true;
}

bool ScannerProcess::readExact(void* buffer, size_t size) {
    size_t totalRead = 0;
    uint8_t* buf = static_cast<uint8_t*>(buffer);

    while (totalRead < size) {
#ifdef PLATFORM_WINDOWS
        DWORD bytesRead;
        if (!ReadFile(readPipe, buf + totalRead, static_cast<DWORD>(size - totalRead),
                     &bytesRead, nullptr) || bytesRead == 0) {
            return false;
        }
        totalRead += bytesRead;
#else
        ssize_t bytesRead = read(readPipe, buf + totalRead, size - totalRead);
        if (bytesRead <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data available, try again later
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            return false;
        }
        totalRead += static_cast<size_t>(bytesRead);
#endif
    }

    return true;
}

bool ScannerProcess::writeExact(const void* buffer, size_t size) {
    size_t totalWritten = 0;
    const uint8_t* buf = static_cast<const uint8_t*>(buffer);

    while (totalWritten < size) {
#ifdef PLATFORM_WINDOWS
        DWORD bytesWritten;
        if (!WriteFile(writePipe, buf + totalWritten, static_cast<DWORD>(size - totalWritten),
                      &bytesWritten, nullptr)) {
            return false;
        }
        totalWritten += bytesWritten;
#else
        ssize_t bytesWritten = write(writePipe, buf + totalWritten, size - totalWritten);
        if (bytesWritten <= 0) {
            return false;
        }
        totalWritten += static_cast<size_t>(bytesWritten);
#endif
    }

    return true;
}

bool ScannerProcess::hasResponse() const {
#ifdef PLATFORM_WINDOWS
    if (readPipe == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD bytesAvailable = 0;
    if (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &bytesAvailable, nullptr)) {
        return bytesAvailable > 0;
    }
    return false;
#else
    if (readPipe == -1) {
        return false;
    }

    // Use select() to check if data is available
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(readPipe, &readSet);

    struct timeval timeout = {};
    int ret = select(readPipe + 1, &readSet, nullptr, nullptr, &timeout);

    return ret > 0 && FD_ISSET(readPipe, &readSet);
#endif
}

#ifndef PLATFORM_WINDOWS
bool ScannerProcess::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

bool ScannerProcess::setCloseOnExec(int fd) {
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags == -1) {
        return false;
    }
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != -1;
}
#endif

// =============================================================================
// SCANNER PROCESS MANAGER IMPLEMENTATION
// =============================================================================

ScannerProcessManager::ScannerProcessManager(uint32_t maxConcurrent)
    : maxConcurrentScans(maxConcurrent)
    , activeScans(0)
{
    pool.resize(maxConcurrentScans);
}

ScannerProcessManager::~ScannerProcessManager() {
    cancelAll();
}

ScannerProcess* ScannerProcessManager::acquire(const char* pluginPath,
                                               IPluginScanner::PluginFormat format) {
    std::lock_guard<std::mutex> lock(poolMutex);
    cleanupTerminated();

    ScannerSlot* slot = findAvailableSlot();
    if (!slot) {
        return nullptr;
    }

    if (!slot->process) {
        slot->process = std::make_unique<ScannerProcess>();
    }

    if (slot->process->spawn(pluginPath, format)) {
        slot->pluginPath = pluginPath;
        slot->format = format;
        slot->inUse = true;
        activeScans.fetch_add(1, std::memory_order_relaxed);
        return slot->process.get();
    }

    return nullptr;
}

void ScannerProcessManager::release(ScannerProcess* process, bool keepProcess) {
    std::lock_guard<std::mutex> lock(poolMutex);
    for (auto& slot : pool) {
        if (slot.process.get() == process) {
            slot.inUse = false;
            slot.pluginPath.clear();
            slot.format = IPluginScanner::PluginFormat::NONE;
            activeScans.fetch_sub(1, std::memory_order_relaxed);

            if (!keepProcess) {
                slot.process.reset();
            }
            break;
        }
    }
}

void ScannerProcessManager::cancelAll() {
    std::lock_guard<std::mutex> lock(poolMutex);
    for (auto& slot : pool) {
        if (slot.process && slot.inUse) {
            slot.process->terminate();
            slot.inUse = false;
            slot.pluginPath.clear();
        }
    }
    activeScans.store(0, std::memory_order_release);
}

ScannerProcessManager::ScannerSlot* ScannerProcessManager::findAvailableSlot() {
    // First try to find an unused slot
    for (auto& slot : pool) {
        if (!slot.inUse) {
            return &slot;
        }
    }
    return nullptr;
}

void ScannerProcessManager::cleanupTerminated() {
    for (auto& slot : pool) {
        if (slot.process && slot.inUse && !slot.process->isProcessRunning()) {
            // Process terminated, clean up slot
            slot.inUse = false;
            slot.pluginPath.clear();
            activeScans.fetch_sub(1, std::memory_order_relaxed);
        }
    }
}

} // namespace Layer2
