#include "glfwApp.h"
#include "linuxPlatform.h"
#include <signal.h>

using namespace Tangram;

int main(int argc, char* argv[]) {

    // Create the windowed app.
    GlfwApp::create(std::make_unique<LinuxPlatform>(), argc, argv);

    // Give it a chance to shutdown cleanly on CTRL-C
    signal(SIGINT, &GlfwApp::stop);

    // Loop until the user closes the window
    GlfwApp::run();

    // Clean up.
    GlfwApp::destroy();

}
