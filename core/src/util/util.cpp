#include "util/util.h"
#include <chrono>

// unaligned access crashes on 32-bit ARM (Android) and seems like a bad idea anyway
#ifndef TANGRAM_NO_STB_IMPL
#define STB_SPRINTF_NOUNALIGNED
#define STB_SPRINTF_IMPLEMENTATION
#endif
#include "stb_sprintf.h"

namespace Tangram {

static char* stb_sprintfcb(const char* buf, void* user, int len) {
    static_cast<std::string*>(user)->append(buf, len);
    return (char*)buf;  // stb_sprintf always calls this with base of buf passed to it
}

// template<class... Args>  std::string fstring(fmt, Args&&... args) -  fn(fmt, std::forward<Args>(args)...)
std::string fstring(const char* fmt, ...) {
    // standard snprintf always returns number of bytes needed to print entire string, whereas stbsp_snprintf
    //  only returns this if passed buf = 0 and count = 0 (otherwise returns actual number written), so we
    //  instead use the callback version, stbsp_vsprintfcb (extra copy, but saves memory)
    char buf[STB_SPRINTF_MIN];
    std::string str;
    va_list va;
    va_start(va, fmt);
    stbsp_vsprintfcb(stb_sprintfcb, &str, buf, fmt, va);
    va_end(va);
    return str;
}

double secSinceEpoch() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration<double>(now).count();
}

}
