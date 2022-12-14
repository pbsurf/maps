#pragma once

#include "tangram.h"
#include <memory>

namespace Tangram {

namespace GlfwApp {

void create(std::unique_ptr<Platform> platform, int argc, char* argv[]);
void parseArgs(int argc, char* argv[]);
void setWireframeMode(bool state);
void run();
void stop(int);
void destroy();

} // namespace GlfwApp

} // namespace Tangram
