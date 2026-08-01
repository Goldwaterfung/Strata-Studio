#include "../common/ipc_protocol.h"
#include <array>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace {

// RAII Socket Handle wrapper to guarantee zero descriptor leaks
class SocketHandle {
public:
    explicit SocketHandle(int fd = -1) noexcept : m_fd(fd) {}
    ~SocketHandle() { reset(); }

    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;

    SocketHandle(SocketHandle&& other) noexcept : m_fd(std::exchange(other.m_fd, -1)) {}
    SocketHandle& operator=(SocketHandle&& other) noexcept {
        if (this != &other) {
            reset();
            m_fd = std::exchange(other.m_fd, -1);
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return m_fd; }
    [[nodiscard]] bool isValid() const noexcept { return m_fd >= 0; }

    void reset(int newFd = -1) noexcept {
        if (m_fd >= 0) {
#ifndef _WIN32
            ::close(m_fd);
#endif
        }
        m_fd = newFd;
    }

private:
    int m_fd{-1};
};

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: daw-cli <command> [options]\n";
        return agentic::ErrorCode::INVALID_ARGS; // Exit Code 70: INVALID_ARGS
    }

    // 1. Reconstruct command string with zero-allocation reservation via std::span
    const std::span<char*> args(argv + 1, static_cast<std::size_t>(argc - 1));
    std::size_t totalLen = 0;
    for (const char* arg : args) {
        if (arg != nullptr) { totalLen += std::strlen(arg) + 1; }
    }

    std::string cmd;
    cmd.reserve(totalLen + 1);
    for (const char* arg : args) {
        if (arg != nullptr) {
            cmd.append(arg).append(" ");
        }
    }
    if (!cmd.empty()) { cmd.back() = '\n'; }

    // 2. Open UNIX Domain Socket with RAII wrapper
    SocketHandle sock{::socket(AF_UNIX, SOCK_STREAM, 0)};
    if (!sock.isValid()) {
        std::cerr << "ERROR: Failed to create IPC socket\n";
        return 1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const std::size_t maxPath = sizeof(addr.sun_path) - 1;
    std::strncpy(addr.sun_path, agentic::DEFAULT_SOCKET_PATH.data(), std::min(agentic::DEFAULT_SOCKET_PATH.size(), maxPath));

    // 3. Connect to main DAW process (Fail-fast if not running)
    if (::connect(sock.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "ERROR 71 DAW_NOT_RUNNING \"Connection failed: No active DAW application detected on "
                  << agentic::DEFAULT_SOCKET_PATH << ". Please launch the DAW application first.\"\n";
        return agentic::ErrorCode::DAW_NOT_RUNNING; // Exit Code 71: DAW_NOT_RUNNING (RAII automatically closes sock)
    }

    // 4. Send command & read response into std::array buffer
    if (::write(sock.get(), cmd.data(), cmd.size()) < 0) {
        std::cerr << "ERROR: Write failed to IPC socket\n";
        return 1;
    }

    std::array<char, 4096> buffer{};
    const ssize_t bytesRead = ::read(sock.get(), buffer.data(), buffer.size() - 1);
    if (bytesRead > 0) {
        std::cout << std::string_view(buffer.data(), static_cast<std::size_t>(bytesRead));
    }

    return 0; // RAII closes socket cleanly on exit
}
