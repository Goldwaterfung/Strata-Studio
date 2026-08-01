#include <iostream>
#include <project_config.h>
#include "application.h"

int main(int argc, char* argv[]) {
    std::cout << config::PROJECT_DISPLAY_NAME << " - Production Grade" << std::endl;
    std::cout << "===================================" << std::endl;
    std::cout << std::endl;

    // Create and initialize application
    app::Application application(argc, argv);

    if (!application.initialize()) {
        std::cerr << "Failed to initialize application" << std::endl;
        return 1;
    }

    // Run main loop
    int exitCode = application.run();

    // Cleanup is handled by Application destructor
    return exitCode;
}
