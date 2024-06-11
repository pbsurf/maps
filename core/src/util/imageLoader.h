#pragma once

#include <stddef.h>
#include <stdint.h>
#include "gl.h"

namespace Tangram {

uint8_t* loadImage(const uint8_t* data, size_t length, int* width, int* height, GLint* pixelfmt, int channels);

}
