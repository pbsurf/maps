#include "gl/hardware.h"

#include "gl.h"
#include "log.h"
#include "platform.h"

#include <cstring>
#include <sstream>
#include <algorithm>
#include <iterator>

namespace Tangram {
namespace Hardware {

bool supportsMapBuffer = false;
bool supportsVAOs = false;
bool supportsTextureNPOT = false;
bool supportsGLRGBA8OES = false;

int32_t maxTextureSize = 2048;
int32_t maxCombinedTextureUnits = 16;
int32_t depthBits = 24;
int32_t glVersion = 200;
static char* s_glExtensions;

bool isAvailable(std::string _extension) {
    return bool(s_glExtensions)
      ? strstr(s_glExtensions, _extension.c_str()) != nullptr
      : false;
}

void printAvailableExtensions() {
    if (s_glExtensions == NULL) {
        LOGW("Extensions string is NULL");
        return;
    }

    std::string exts(s_glExtensions);
    std::stringstream ss(exts);

    ss >> std::noskipws;
    std::string extension;
    LOGD("GL Extensions available: ");
    while (std::getline(ss, extension, ' ')) {
        LOGD("\t - %s", extension.c_str());
    }
}

void loadExtensions() {
    s_glExtensions = (char*) GL::getString(GL_EXTENSIONS);

    if (s_glExtensions == NULL) {
        LOGW("glGetString( GL_EXTENSIONS ) returned NULL");
        s_glExtensions = "";
    }

    supportsMapBuffer = /*glVersion >= 300 || */ isAvailable("mapbuffer");
    supportsVAOs = glVersion >= 300 || isAvailable("vertex_array_object");
    supportsTextureNPOT = glVersion >= 300 || isAvailable("texture_non_power_of_two");
    supportsGLRGBA8OES = glVersion >= 300 || isAvailable("rgb8_rgba8");

    if (glVersion < 300) {
        LOG("Driver supports map buffer: %d", supportsMapBuffer);
        LOG("Driver supports vaos: %d", supportsVAOs);
        LOG("Driver supports rgb8_rgba8: %d", supportsGLRGBA8OES);
        LOG("Driver supports NPOT texture: %d", supportsTextureNPOT);
    }

    // find extension symbols if needed
    initGLExtensions();
}

void loadCapabilities() {
    GL::getIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    GL::getIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxCombinedTextureUnits);
    GL::getIntegerv(GL_DEPTH_BITS, &depthBits);

    float ver = 3.0f; // assume GL 3
    const char* verstr = (const char*) GL::getString(GL_VERSION);
    if (verstr) {
        sscanf(verstr, "%*[^123456789]%f", &ver);
    }
    glVersion = ver*100 + 0.5f;

    LOG("Hardware max texture size %d", maxTextureSize);
    LOG("Hardware max combined texture units %d", maxCombinedTextureUnits);
    LOG("Framebuffer depth bits %d", depthBits);
    LOG("OpenGL version %.2f (%s)", glVersion/100.0, verstr ? verstr : "???");
}

}
}
