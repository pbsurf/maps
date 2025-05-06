#pragma once

#include <string>
#include <cstdint>

namespace Tangram {
namespace Hardware {

extern bool supportsMapBuffer;
extern bool supportsVAOs;
extern bool supportsTextureNPOT;
extern bool supportsGLRGBA8OES;
extern int32_t maxTextureSize;
extern int32_t maxCombinedTextureUnits;
extern int32_t depthBits;
extern int32_t glVersion;

void loadCapabilities();
void loadExtensions();
bool isAvailable(std::string _extension);
void printAvailableExtensions();

}
}
