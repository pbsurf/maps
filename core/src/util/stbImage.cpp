#include "imageLoader.h"
#include "log.h"
#include <memory>

#ifndef TANGRAM_NO_STB_IMPL
// Enable only JPEG, PNG, GIF, TGA and PSD
#define STBI_NO_BMP
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM

#define STB_IMAGE_IMPLEMENTATION
#endif
#include "stb_image.h"

#define TANGRAM_TIFF_SUPPORT

#ifdef TANGRAM_TIFF_SUPPORT
#define TINY_DNG_LOADER_IMPLEMENTATION
#define TINY_DNG_LOADER_ENABLE_ZIP
#define TINY_DNG_NO_EXCEPTION
#define TINY_DNG_LOADER_NO_STB_IMAGE_INCLUDE  // this is simpler than #undef STB_IMAGE_IMPLEMENTATION
#include "tinydng/tiny_dng_loader.h"
#endif

namespace Tangram {

static uint8_t* flipImage(const uint8_t* data, int width, int height, int bpp)
{
    // need to flip image vertically for OpenGL coordinate system
    // stbi_set_flip_vertically_on_load flips image in place, requiring 3x memcpy per row; we'd also need to
    //  switch to stbi_set_flip_vertically_on_load_thread to avoid conflict w/ other users of stb_image
    uint8_t* flipped = reinterpret_cast<uint8_t*>(std::malloc(width*height*bpp));
    int rowSize = width*bpp;
    for(int y = 0; y < height; ++y) {
        const uint8_t* src = &data[y*rowSize];
        uint8_t* dst = &flipped[(height - y - 1)*rowSize];
        std::memcpy(dst, src, rowSize);
    }
    return flipped;
}

struct malloc_deleter { void operator()(GLubyte* x) { std::free(x); } };

uint8_t* loadImage(const uint8_t* data, size_t length, int* width, int* height, GLint* pixelfmt, int channels)
{
    if (length < 2) return nullptr;
    // check for TIFF
    if ((data[0] == 'I' && data[1] == 'I') || (data[0] == 'M' && data[1] == 'M')) {
#ifdef TANGRAM_TIFF_SUPPORT
        std::string warn, err;
        std::vector<tinydng::DNGImage> images;
        std::vector<tinydng::FieldInfo> custom_fields;

        bool ret = tinydng::LoadDNGFromMemory((const char*)data, length, custom_fields, &images, &warn, &err);
        if (!ret || images.empty() || images[0].data.empty()) {
            LOGE("Error loading TIFF: %s", err.c_str());
            return nullptr;
        }

        auto& image = images[0];
        GLint fmt = 0;
        if (image.sample_format == tinydng::SAMPLEFORMAT_IEEEFP) {
            if (image.samples_per_pixel == 1 && image.bits_per_sample == 32)
                fmt = GL_R32F;
        } else if (image.bits_per_sample == 8) {
            if (image.samples_per_pixel == 1) fmt = GL_R8;
            else if (image.samples_per_pixel == 3) fmt = GL_RGB8;
            else if (image.samples_per_pixel == 4) fmt = GL_RGBA8;
        }
        if(!fmt) {
            LOGE("Unsupported TIFF: %d bits per sample, %d samples per pixel",
                 image.bits_per_sample, image.samples_per_pixel);
            return nullptr;
        }

        *width = image.width;
        *height = image.height;
        *pixelfmt = fmt;
        int bpp = image.samples_per_pixel*image.bits_per_sample/8;
        return flipImage(image.data.data(), image.width, image.height, bpp);
#else
        LOGE("TIFF support disabled - recompile with TANGRAM_TIFF_SUPPORT defined.");
        return nullptr;
#endif
    }

    int channelsInFile = 0;
    std::unique_ptr<uint8_t, malloc_deleter> pixels(
        stbi_load_from_memory(data, int(length), width, height, &channelsInFile, channels));
    if (!pixels) {
        LOGE("Error loading image data: %dx%d bpp:%d/%d - %s",
             width, height, channelsInFile, channels, stbi_failure_reason());
        return nullptr;
    }

    if (channels == 1) *pixelfmt = GL_R8;
    else if (channels == 3) *pixelfmt = GL_RGB8;
    else if (channels == 4) *pixelfmt = GL_RGBA8;

    return flipImage(pixels.get(), *width, *height, channels);
}

}
