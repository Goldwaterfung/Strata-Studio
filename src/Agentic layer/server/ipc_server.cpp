#include "ipc_server.h"
#include <array>
#include <cstring>
#include <iostream>
#include <utility>

#ifndef _WIN32
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace {

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

namespace agentic {

IPCServer::IPCServer(std::shared_ptr<CommandDispatcher> dispatcher, std::string_view socketPath)
    : m_dispatcher(std::move(dispatcher)), m_socketPath(socketPath) {}

IPCServer::~IPCServer() {
    stop();
}

void IPCServer::start() {
    if (m_running.exchange(true)) {
        return; // Already running
    }
    m_serverThread = std::thread([this]() { runServerLoop(); });
}

void IPCServer::stop() {
    if (!m_running.exchange(false)) {
        return;
    }
    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }
}

void IPCServer::runServerLoop() {
#ifndef _WIN32
    SocketHandle serverSock{::socket(AF_UNIX, SOCK_STREAM, 0)};
    if (!serverSock.isValid()) {
        m_running.store(false);
        return;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const std::size_t maxPath = sizeof(addr.sun_path) - 1;
    std::strncpy(addr.sun_path, m_socketPath.c_str(), std::min(m_socketPath.size(), maxPath));

    ::unlink(m_socketPath.c_str());
    if (::bind(serverSock.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        m_running.store(false);
        return;
    }
    if (::listen(serverSock.get(), 5) < 0) {
        m_running.store(false);
        return;
    }

    while (m_running.load()) {
        struct pollfd pfd{};
        pfd.fd = serverSock.get();
        pfd.events = POLLIN;

        const int pollRes = ::poll(&pfd, 1, 100);
        if (pollRes <= 0 || !(pfd.revents & POLLIN)) {
            continue;
        }

        SocketHandle clientSock{::accept(serverSock.get(), nullptr, nullptr)};
        if (!clientSock.isValid()) {
            continue;
        }

        std::array<char, 4096> buffer{};
        const ssize_t bytes = ::read(clientSock.get(), buffer.data(), buffer.size() - 1);
        if (bytes > 0) {
            const std::string_view commandLine(buffer.data(), static_cast<std::size_t>(bytes));
            const std::string response = m_dispatcher->executeCommand(commandLine);
            ::write(clientSock.get(), response.data(), response.size());
        }
    }

    ::unlink(m_socketPath.c_str());
#endif
}

} // namespace agentic
