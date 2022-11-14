#pragma once

#include <vector>
#include <string.h>

namespace Tangram {

int zlib_inflate(const char* _data, size_t _size, std::vector<char>& dst);

}
