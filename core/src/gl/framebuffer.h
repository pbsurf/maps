#pragma once

#include <memory>
#include <vector>

#include "glm/vec2.hpp"
#include "glm/vec4.hpp"
#include "gl.h"
#include "util/color.h"

namespace Tangram {

class RenderState;
class RenderTexture;

class FrameBuffer {

public:

    FrameBuffer(int _width, int _height, bool _colorRenderBuffer = true, GLenum _pixelFormat = GL_RGBA8);

    ~FrameBuffer();

    bool applyAsRenderTarget(RenderState& _rs, ColorF _clearColor = ColorF());

    static void apply(RenderState& _rs, GLuint _handle, glm::vec4 _viewport, ColorF _clearColor);

    bool valid() const { return m_valid; }

    int getWidth() const { return m_width; }

    int getHeight() const { return m_height; }

    void bind(RenderState& _rs) const;

    GLuint readAt(float _normalizedX, float _normalizedY) const;

    struct PixelRect {
        std::vector<GLuint> pixels;
        int32_t left = 0, bottom = 0, width = 0, height = 0;
    };

    PixelRect readRect(float _normalizedX, float _normalizedY, float _normalizedW, float _normalizedH) const;

    void drawDebug(RenderState& _rs, glm::vec2 _dim);

    GLuint getHandle() const { return m_glFrameBufferHandle; }
    GLuint getTextureHandle() const;

private:

    void init(RenderState& _rs);

    std::unique_ptr<RenderTexture> m_texture;

    RenderState* m_rs = nullptr;

    GLuint m_glFrameBufferHandle;

    GLuint m_glDepthRenderBufferHandle;

    GLuint m_glColorRenderBufferHandle;

    GLenum m_pixelFormat;

    bool m_valid;

    bool m_colorRenderBuffer;

    int m_width;

    int m_height;

};

}
