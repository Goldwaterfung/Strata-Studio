#pragma once

#include <memory>
#include <vector>

#include "Middle Bridge/boot/iboot_controller.h"

class QApplication;
class QTimer;

namespace presentation::views {
class MainWindow;
class SplashScreen;
}

namespace agentic {
class CommandDispatcher;
class IPCServer;
}

namespace app {

/**
 * @brief Application lifecycle manager
 *
 * Manages the startup sequence, main loop execution, and graceful shutdown
 * of the DAW application. This is the top-level orchestrator that coordinates
 * all architectural layers.
 */
class Application : public bridge::IBootController::IListener {
public:
    Application(int argc, char* argv[]);
    ~Application() override;

    // Prevent copying
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // === IBootController::IListener Overrides ===
    void onBootStageChanged(bridge::BootStage /*stage*/, const std::string& /*statusText*/) override {}
    void onBootProgressUpdated(float /*progress*/) override {}
    void onBootCompleted() override;
    void onBootFailed(const std::string& errorMessage) override;

    /**
     * @brief Initialize all layers in dependency order (L1 → L2 → L3 → L4 → L5 → L6 → L7)
     * @return true if initialization succeeded
     */
    bool initialize();

    /**
     * @brief Run the main application loop
     * @return Exit code
     */
    int run();

    /**
     * @brief Shutdown all layers in reverse order
     */
    void shutdown();

private:
    int m_argc;
    char** m_argv;
    std::unique_ptr<QApplication> m_qapp;
    std::unique_ptr<presentation::views::MainWindow> m_mainWindow;
    std::unique_ptr<bridge::IBootController> m_bootController;
    std::unique_ptr<presentation::views::SplashScreen> m_splashScreen;
    QTimer* m_bootTimer{nullptr};

    std::shared_ptr<agentic::CommandDispatcher> m_commandDispatcher;
    std::unique_ptr<agentic::IPCServer> m_ipcServer;

    bool m_initialized = false;

    // Layer initialization helpers
    bool initializeLayer1();
    bool initializeLayer2();
    bool initializeLayer3();
    bool initializeLayer4();
    bool initializeLayer5();
    bool initializeLayer6();
    bool initializeLayer7();
    bool initializeLayer8();

    // Layer shutdown helpers
    void shutdownLayer8();
    void shutdownLayer7();
    void shutdownLayer6();
    void shutdownLayer5();
    void shutdownLayer4();
    void shutdownLayer3();
    void shutdownLayer2();
    void shutdownLayer1();
};

} // namespace app
