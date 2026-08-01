#pragma once

#include "command_dispatcher.h"
#include "../common/ipc_protocol.h"
#include <atomic>
#include <memory>
#include <string_view>
#include <thread>

namespace agentic {

class IPCServer {
public:
    explicit IPCServer(std::shared_ptr<CommandDispatcher> dispatcher,
                      std::string_view socketPath = DEFAULT_SOCKET_PATH);
    ~IPCServer();

    IPCServer(const IPCServer&) = delete;
    IPCServer& operator=(const IPCServer&) = delete;

    void start();
    void stop();

    [[nodiscard]] bool isRunning() const noexcept { return m_running.load(); }

private:
    void runServerLoop();

    std::shared_ptr<CommandDispatcher> m_dispatcher;
    std::string m_socketPath;
    std::atomic<bool> m_running{false};
    std::thread m_serverThread;
};

} // namespace agentic
