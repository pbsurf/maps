#pragma once

#include <string>
#include <cstdint>

namespace Tangram {
namespace Hardware {

extern bool supportsMapBuffer;
extern bool supportsVAOs;
extern bool supportsTextureNPOT;
extern bool supportsGLRGBA8OES;
extern uint32_t maxTextureSize;
extern uint32_t maxCombinedTextureUnits;
extern uint32_t depthBits;
extern uint32_t glVersion;

void loadCapabilities();
void loadExtensions();
bool isAvailable(std::string _extension);
void printAvailableExtensions();

}
}
