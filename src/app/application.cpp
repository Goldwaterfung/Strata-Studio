#include "application.h"
#include "composition_root.h"
#include <QApplication>
#include <QTimer>
#include "Presentation/views/main_window.h"
#include "Presentation/views/playlist/PlaylistWindow.h"
#include "Presentation/views/boot/splash_screen.h"
#include "Presentation/views/theme.h"
#include "Middle Bridge/boot/boot_controller.h"
#include "Agentic layer/server/ipc_server.h"
#include "Agentic layer/server/command_dispatcher.h"
#include <iostream>

namespace app {

Application::Application(int argc, char* argv[])
    : m_argc(argc), m_argv(argv) {}

Application::~Application() {
    if (m_initialized) {
        shutdown();
    }
}

bool Application::initialize() {
    std::cout << "Application: Initializing DAW..." << std::endl;

    // Initialize layers in dependency order
    if (!initializeLayer1()) {
        std::cerr << "Application: Failed to initialize Layer 1 (Hardware/OS Abstraction)" << std::endl;
        return false;
    }

    if (!initializeLayer2()) {
        std::cerr << "Application: Failed to initialize Layer 2 (Core Infrastructure)" << std::endl;
        shutdownLayer1();
        return false;
    }

    if (!initializeLayer3()) {
        std::cerr << "Application: Failed to initialize Layer 3 (Core Audio Engine)" << std::endl;
        shutdownLayer2();
        shutdownLayer1();
        return false;
    }

    if (!initializeLayer4()) {
        std::cerr << "Application: Failed to initialize Layer 4 (DSP Processing Nodes)" << std::endl;
        shutdownLayer3();
        shutdownLayer2();
        shutdownLayer1();
        return false;
    }

    if (!initializeLayer5()) {
        std::cerr << "Application: Failed to initialize Layer 5 (Musical Composition)" << std::endl;
        shutdownLayer4();
        shutdownLayer3();
        shutdownLayer2();
        shutdownLayer1();
        return false;
    }

    if (!initializeLayer6()) {
        std::cerr << "Application: Failed to initialize Layer 6 (Media Management)" << std::endl;
        shutdownLayer5();
        shutdownLayer4();
        shutdownLayer3();
        shutdownLayer2();
        shutdownLayer1();
        return false;
    }

    if (!initializeLayer7()) {
        std::cerr << "Application: Failed to initialize Layer 7 (Presentation)" << std::endl;
        shutdownLayer6();
        shutdownLayer5();
        shutdownLayer4();
        shutdownLayer3();
        shutdownLayer2();
        shutdownLayer1();
        return false;
    }

    if (!initializeLayer8()) {
        std::cerr << "Application: Failed to initialize Layer 8 (Agentic Interface Layer)" << std::endl;
        shutdownLayer7();
        shutdownLayer6();
        shutdownLayer5();
        shutdownLayer4();
        shutdownLayer3();
        shutdownLayer2();
        shutdownLayer1();
        return false;
    }

    // Instantiate the asynchronous BootController and display our premium SplashScreen
    m_bootController = std::make_unique<bridge::BootController>(&CompositionRoot::instance());
    m_bootController->registerListener(this);

    m_splashScreen = std::make_unique<presentation::views::SplashScreen>(m_bootController.get());

    m_initialized = true;
    std::cout << "Application: Deferred initialization via Splash Screen registered." << std::endl;
    return true;
}

int Application::run() {
    if (!m_initialized) {
        std::cerr << "Application: Cannot run - not initialized" << std::endl;
        return 1;
    }

    std::cout << "Application: Starting Qt Event Loop with Splash Screen..." << std::endl;

    if (m_splashScreen) {
        m_splashScreen->show();
    }

    if (m_bootController) {
        m_bootController->startBootSequence();
    }

    // Set up a QTimer on the main thread to drive the cooperative boot state machine
    m_bootTimer = new QTimer(m_qapp.get());
    QObject::connect(m_bootTimer, &QTimer::timeout, [this]() {
        if (m_bootController) {
            m_bootController->tick();
        }
    });
    m_bootTimer->start(30); // Tick every 30 milliseconds

    if (m_splashScreen) {
        QObject::connect(m_splashScreen.get(), &presentation::views::SplashScreen::sigRetryBoot, [this]() {
            if (m_bootTimer) {
                m_bootTimer->start(30);
            }
        });
    }

    int exitCode = 0;
    if (m_qapp) {
        exitCode = m_qapp->exec();
    }

    std::cout << "Application: Qt Event Loop terminated" << std::endl;
    return exitCode;
}

void Application::shutdown() {
    if (!m_initialized) {
        return;
    }

    std::cout << "Application: Shutting down..." << std::endl;

    // Shutdown layers in reverse order
    shutdownLayer8();
    shutdownLayer7();
    shutdownLayer6();
    shutdownLayer5();
    shutdownLayer4();
    shutdownLayer3();
    shutdownLayer2();
    shutdownLayer1();

    m_initialized = false;
    std::cout << "Application: Shutdown complete" << std::endl;
}

bool Application::initializeLayer1() {
    // TODO: Initialize hardware/OS abstraction layer
    // - Audio I/O
    // - MIDI I/O
    // - Filesystem
    std::cout << "Application: Initializing Layer 1 (Hardware/OS Abstraction)..." << std::endl;
    return true;
}

bool Application::initializeLayer2() {
    // TODO: Initialize core infrastructure layer
    // - Memory coordinator
    // - Bridges (mutation, telemetry)
    // - State management
    std::cout << "Application: Initializing Layer 2 (Core Infrastructure)..." << std::endl;
    return true;
}

bool Application::initializeLayer3() {
    // TODO: Initialize core audio engine layer
    // - Transport
    // - Scheduler
    // - Plugin host
    std::cout << "Application: Initializing Layer 3 (Core Audio Engine)..." << std::endl;
    return true;
}

bool Application::initializeLayer4() {
    // TODO: Initialize DSP processing nodes layer
    // - Channel strips
    // - Buses
    // - Plugins
    std::cout << "Application: Initializing Layer 4 (DSP Processing Nodes)..." << std::endl;
    return true;
}

bool Application::initializeLayer5() {
    // TODO: Initialize musical composition layer
    // - Arranger
    // - MIDI
    // - Tracks
    std::cout << "Application: Initializing Layer 5 (Musical Composition)..." << std::endl;
    return true;
}

bool Application::initializeLayer6() {
    // TODO: Initialize media management layer
    // - Browser
    // - Library
    // - Codecs
    std::cout << "Application: Initializing Layer 6 (Media Management)..." << std::endl;
    return true;
}

bool Application::initializeLayer7() {
    std::cout << "Application: Initializing Layer 7 (Presentation)..." << std::endl;
    
    // Enable the virtual keyboard input method globally
    qputenv("QT_IM_MODULE", QByteArray("qtvirtualkeyboard"));

    m_qapp = std::make_unique<QApplication>(m_argc, m_argv);
    m_qapp->setQuitOnLastWindowClosed(false);
    
    // Register custom application fonts before loading any views
    presentation::theme::Font::initialize();
    
    return true;
}

bool Application::initializeLayer8() {
    std::cout << "Application: Initializing Layer 8 (Agentic Interface Layer)..." << std::endl;
    m_commandDispatcher = std::make_shared<agentic::CommandDispatcher>();
    m_ipcServer = std::make_unique<agentic::IPCServer>(m_commandDispatcher);
    m_ipcServer->start();
    std::cout << "Application: IPC Server daemon listening on " << agentic::DEFAULT_SOCKET_PATH << std::endl;
    return true;
}

void Application::shutdownLayer8() {
    std::cout << "Application: Shutting down Layer 8 (Agentic Interface Layer)..." << std::endl;
    if (m_ipcServer) {
        m_ipcServer->stop();
        m_ipcServer.reset();
    }
    m_commandDispatcher.reset();
}

void Application::shutdownLayer7() {
    std::cout << "Application: Shutting down Layer 7 (Presentation)..." << std::endl;
    m_mainWindow.reset();
    m_qapp.reset();
}

void Application::shutdownLayer6() {
    std::cout << "Application: Shutting down Layer 6 (Media Management)..." << std::endl;
}

void Application::shutdownLayer5() {
    std::cout << "Application: Shutting down Layer 5 (Musical Composition)..." << std::endl;
}

void Application::shutdownLayer4() {
    std::cout << "Application: Shutting down Layer 4 (DSP Processing Nodes)..." << std::endl;
}

void Application::shutdownLayer3() {
    std::cout << "Application: Shutting down Layer 3 (Core Audio Engine)..." << std::endl;
}

void Application::shutdownLayer2() {
    std::cout << "Application: Shutting down Layer 2 (Core Infrastructure)..." << std::endl;
}

void Application::shutdownLayer1() {
    std::cout << "Application: Shutting down Layer 1 (Hardware/OS Abstraction)..." << std::endl;
}

void Application::onBootCompleted() {
    std::cout << "Application: Asynchronous boot completed successfully!" << std::endl;

    if (m_bootTimer) {
        m_bootTimer->stop();
        m_bootTimer->deleteLater();
        m_bootTimer = nullptr;
    }

    // Now instantiate MainWindow on the main GUI thread
    m_mainWindow = std::make_unique<presentation::views::MainWindow>();

    // Inject controller dependencies from CompositionRoot
    CompositionRoot& compositionRoot = CompositionRoot::instance();

    if (m_commandDispatcher) {
        m_commandDispatcher->setControllers({
            compositionRoot.getTrackController(),
            compositionRoot.getTimelineController()
        });
    }

    m_mainWindow->setBrowserController(compositionRoot.getBrowserController());
    m_mainWindow->setHardwareSettingsFacade(compositionRoot.getHardwareSettingsFacade());
    m_mainWindow->setMidiEditorController(compositionRoot.getMidiEditorController());
    m_mainWindow->setTopPanelControllers(
        compositionRoot.getTimelineController(),
        compositionRoot.getInputModeController(),
        compositionRoot.getWorkspaceController()
    );

    // Instantiate and inject Playlist Controllers
    presentation::views::PlaylistWindow::Controllers playlistCtrls;
    playlistCtrls.arrangement = compositionRoot.getArrangementController();
    playlistCtrls.track = compositionRoot.getTrackController();
    playlistCtrls.timeline = compositionRoot.getTimelineController();
    playlistCtrls.automation = compositionRoot.getAutomationController();
    playlistCtrls.inputMode = compositionRoot.getInputModeController();
    playlistCtrls.metering = compositionRoot.getMeteringProvider();
    playlistCtrls.waveform = compositionRoot.getWaveformCacheProvider();
    playlistCtrls.patternData = compositionRoot.getPatternDataProvider();
    playlistCtrls.browser = compositionRoot.getBrowserController();
    playlistCtrls.sessionManager = compositionRoot.getSessionManager();
    playlistCtrls.lifecycle = compositionRoot.getProjectLifecycleController();
    playlistCtrls.arrangementManager = compositionRoot.getArrangementManagerController();
    playlistCtrls.render = compositionRoot.getRenderController();
    playlistCtrls.workspace = compositionRoot.getWorkspaceController();

    m_mainWindow->setPlaylistControllers(playlistCtrls);

    // Smoothly show MainWindow and close SplashScreen
    m_mainWindow->show();

    // Process pending events to ensure MainWindow's show event is registered
    m_qapp->processEvents();

    // Restore default exit behavior when MainWindow is closed
    m_qapp->setQuitOnLastWindowClosed(true);

    if (m_splashScreen) {
        m_splashScreen->hide();
        m_splashScreen.release()->deleteLater();
    }
}

void Application::onBootFailed(const std::string& errorMessage) {
    std::cerr << "Application: Asynchronous boot failed: " << errorMessage << std::endl;
    if (m_bootTimer) {
        m_bootTimer->stop();
    }
}

} // namespace app
