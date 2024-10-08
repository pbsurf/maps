#include "gl/renderState.h"

#include "gl/vertexLayout.h"
#include "gl/glError.h"
#include "gl/hardware.h"
#include "gl/texture.h"
#include "log.h"
#include "platform.h"

#include <limits>

namespace Tangram {

RenderState::RenderState() {

    m_blending = { 0, false };
    m_culling = { 0, false };
    m_depthMask = { 0, false };
    m_depthTest = { 0, false };
    m_stencilTest = { 0, false };
    m_blendingFunc = { 0, 0, false };
    m_stencilMask = { 0, false };
    m_stencilFunc = { 0, 0, 0, false };
    m_stencilOp = { 0, 0, 0, false };
    m_colorMask = { 0, 0, 0, 0, false };
    m_frontFace = { 0, false };
    m_cullFace = { 0, false };
    m_vertexBuffer = { 0, false };
    m_indexBuffer = { 0, false };
    m_program = { 0, false };
    m_clearColor = { 0., 0., 0., 0., false };
    m_defaultOpaqueClearColor = { 0., 0., 0., false };
    m_texture = { 0, 0, false };
    m_textureUnit = { 0, false };
    m_framebuffer = { 0, false };
    m_viewport = { 0, 0, 0, 0, false };

}

void RenderState::flushResourceDeletion() {
    std::lock_guard<std::mutex> guard(m_deletionListMutex);

    if (m_VAODeletionList.size()) {
        GL::deleteVertexArrays(m_VAODeletionList.size(), m_VAODeletionList.data());
        m_VAODeletionList.clear();
    }
    if (m_textureDeletionList.size()) {
        GL::deleteTextures(m_textureDeletionList.size(), m_textureDeletionList.data());
        m_textureDeletionList.clear();
    }
    if (m_bufferDeletionList.size()) {
        GL::deleteBuffers(m_bufferDeletionList.size(), m_bufferDeletionList.data());
        m_bufferDeletionList.clear();
    }
    if (m_framebufferDeletionList.size()) {
        GL::deleteFramebuffers(m_framebufferDeletionList.size(), m_framebufferDeletionList.data());
        m_framebufferDeletionList.clear();
    }
    if (m_programDeletionList.size()) {
        for (GLuint program : m_programDeletionList) {
            GL::deleteProgram(program);
        }
        m_programDeletionList.clear();
    }
}

void RenderState::queueFramebufferDeletion(GLuint framebuffer) {
    std::lock_guard<std::mutex> guard(m_deletionListMutex);
    m_framebufferDeletionList.push_back(framebuffer);
}

void RenderState::queueProgramDeletion(GLuint program) {
    std::lock_guard<std::mutex> guard(m_deletionListMutex);
    m_programDeletionList.push_back(program);
}

void RenderState::queueTextureDeletion(GLuint texture) {
    std::lock_guard<std::mutex> guard(m_deletionListMutex);
    m_textureDeletionList.push_back(texture);
}

void RenderState::queueVAODeletion(size_t count, GLuint* vao) {
    std::lock_guard<std::mutex> guard(m_deletionListMutex);
    m_VAODeletionList.insert(m_VAODeletionList.end(), vao, vao + count);
}

void RenderState::queueBufferDeletion(size_t count, GLuint* buffers) {
    std::lock_guard<std::mutex> guard(m_deletionListMutex);
    m_bufferDeletionList.insert(m_bufferDeletionList.end(), buffers, buffers + count);
}

GLuint RenderState::getTextureUnit(GLuint _unit) {
    return GL_TEXTURE0 + _unit;
}

RenderState::~RenderState() {

    deleteQuadIndexBuffer();
    flushResourceDeletion();

    for (auto& s : vertexShaders) {
        GL::deleteShader(s.second);
    }
    vertexShaders.clear();

    for (auto& s : fragmentShaders) {
        GL::deleteShader(s.second);
    }
    fragmentShaders.clear();
}

void RenderState::invalidate() {
    invalidateStates();
    invalidateHandles();
}

void RenderState::invalidateStates() {
    m_blending.set = false;
    m_blendingFunc.set = false;
    m_clearColor.set = false;
    m_colorMask.set = false;
    m_cullFace.set = false;
    m_culling.set = false;
    m_depthTest.set = false;
    m_depthMask.set = false;
    m_frontFace.set = false;
    m_stencilTest.set = false;
    m_stencilMask.set = false;
    m_program.set = false;
    m_indexBuffer.set = false;
    m_vertexBuffer.set = false;
    m_texture.set = false;
    m_textureUnit.set = false;
    m_viewport.set = false;
    m_framebuffer.set = false;

    attributeBindings.fill(0);

    GL::depthFunc(GL_LESS);
    GL::clearDepth(1.0);
    GL::depthRange(0.0, 1.0);
}

void RenderState::invalidateHandles() {
    // The shader handles in our caches are no longer valid,
    // so clear them without deleting.
    vertexShaders.clear();
    fragmentShaders.clear();

    // The handles queued for deletion are no longer valid,
    // so clear them without deleting.
    {
        std::lock_guard<std::mutex> guard(m_deletionListMutex);
        m_VAODeletionList.clear();
        m_textureDeletionList.clear();
        m_bufferDeletionList.clear();
        m_framebufferDeletionList.clear();
        m_programDeletionList.clear();
        m_shaderDeletionList.clear();
    }
}

void RenderState::cacheDefaultFramebuffer() {
    GL::getIntegerv(GL_FRAMEBUFFER_BINDING, &m_defaultFramebuffer);
}

int RenderState::nextAvailableTextureUnit() {
    if (m_nextTextureUnit >= Hardware::maxCombinedTextureUnits) {
        LOGE("Too many combined texture units are being used");
        LOGE("GPU supports %d combined texture units", Hardware::maxCombinedTextureUnits);
    }

    return ++m_nextTextureUnit;
}

void RenderState::releaseTextureUnit() {
    m_nextTextureUnit--;
}

int RenderState::currentTextureUnit() {
    return m_nextTextureUnit;
}

void RenderState::resetTextureUnit(int _unit) {
    m_nextTextureUnit = _unit;
}

inline void setGlFlag(GLenum flag, GLboolean enable) {
    if (enable) {
        GL::enable(flag);
    } else {
        GL::disable(flag);
    }
}

bool RenderState::blending(GLboolean enable) {
    if (!m_blending.set || m_blending.enabled != enable) {
        m_blending = { enable, true };
        setGlFlag(GL_BLEND, enable);
        return false;
    }
    return true;
}

bool RenderState::blendingFunc(GLenum sfactor, GLenum dfactor) {
    if (!m_blendingFunc.set || m_blendingFunc.sfactor != sfactor || m_blendingFunc.dfactor != dfactor) {
        m_blendingFunc = { sfactor, dfactor, true };
        GL::blendFunc(sfactor, dfactor);
        return false;
    }
    return true;
}

void RenderState::clearDefaultOpaqueColor() {
    if (m_defaultOpaqueClearColor.set) {
        clearColor(m_defaultOpaqueClearColor.r, m_defaultOpaqueClearColor.g, m_defaultOpaqueClearColor.b, 1.0);
    }
}

bool RenderState::defaultOpaqueClearColor() {
    return m_defaultOpaqueClearColor.set;
}

void RenderState::defaultOpaqueClearColor(GLclampf r, GLclampf g, GLclampf b) {
    m_defaultOpaqueClearColor = { r, g, b, true };
}

bool RenderState::clearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    if (!m_clearColor.set || m_clearColor.r != r || m_clearColor.g != g || m_clearColor.b != b || m_clearColor.a != a) {
        m_clearColor = { r, g, b, a, true };
        GL::clearColor(r, g, b, a);
        return false;
    }
    return true;
}

bool RenderState::colorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    if (!m_colorMask.set || m_colorMask.r != r || m_colorMask.g != g || m_colorMask.b != b || m_colorMask.a != a) {
        m_colorMask = { r, g, b, a, true };
        GL::colorMask(r, g, b, a);
        return false;
    }
    return true;
}

bool RenderState::cullFace(GLenum face) {
    if (!m_cullFace.set || m_cullFace.face != face) {
        m_cullFace = { face, true };
        GL::cullFace(face);
        return false;
    }
    return true;
}

bool RenderState::culling(GLboolean enable) {
    if (!m_culling.set || m_culling.enabled != enable) {
        m_culling = { enable, true };
        setGlFlag(GL_CULL_FACE, enable);
        return false;
    }
    return true;
}

bool RenderState::depthTest(GLboolean enable) {
    if (!m_depthTest.set || m_depthTest.enabled != enable) {
        m_depthTest = { enable, true };
        setGlFlag(GL_DEPTH_TEST, enable);
        return false;
    }
    return true;
}

bool RenderState::depthMask(GLboolean enable) {
    if (!m_depthMask.set || m_depthMask.enabled != enable) {
        m_depthMask = { enable, true };
        GL::depthMask(enable);
        return false;
    }
    return true;
}

bool RenderState::frontFace(GLenum face) {
    if (!m_frontFace.set || m_frontFace.face != face) {
        m_frontFace = { face, true };
        GL::frontFace(face);
        return false;
    }
    return true;
}

bool RenderState::stencilMask(GLuint mask) {
    if (!m_stencilMask.set || m_stencilMask.mask != mask) {
        m_stencilMask = { mask, true };
        GL::stencilMask(mask);
        return false;
    }
    return true;
}

bool RenderState::stencilFunc(GLenum func, GLint ref, GLuint mask) {
    if (!m_stencilFunc.set || m_stencilFunc.func != func || m_stencilFunc.ref != ref || m_stencilFunc.mask != mask) {
        m_stencilFunc = { func, ref, mask, true };
        GL::stencilFunc(func, ref, mask);
        return false;
    }
    return true;
}

bool RenderState::stencilOp(GLenum sfail, GLenum spassdfail, GLenum spassdpass) {
    if (!m_stencilOp.set || m_stencilOp.sfail != sfail || m_stencilOp.spassdfail != spassdfail || m_stencilOp.spassdpass != spassdpass) {
        m_stencilOp = { sfail, spassdfail, spassdpass, true };
        GL::stencilOp(sfail, spassdfail, spassdpass);
        return false;
    }
    return true;
}

bool RenderState::stencilTest(GLboolean enable) {
    if (!m_stencilTest.set || m_stencilTest.enabled != enable) {
        m_stencilTest = { enable, true };
        setGlFlag(GL_STENCIL_TEST, enable);
        return false;
    }
    return true;
}

bool RenderState::shaderProgram(GLuint program) {
    if (!m_program.set || m_program.program != program) {
        m_program = { program, true };
        GL::useProgram(program);
        return false;
    }
    return true;
}

void RenderState::texture(GLuint handle, GLuint unit, GLenum target) {
    if (!m_textureUnit.set || m_textureUnit.unit != unit) {
        m_textureUnit = { unit, true };
        // Our cached texture handle is irrelevant on the new unit, so unset it.
        m_texture.set = false;
        GL::activeTexture(getTextureUnit(unit));
    }
    if (!m_texture.set || m_texture.target != target || m_texture.handle != handle) {
        m_texture = { target, handle, true };
        GL::bindTexture(target, handle);
    }
}

bool RenderState::vertexBuffer(GLuint handle) {
    if (!m_vertexBuffer.set || m_vertexBuffer.handle != handle) {
        m_vertexBuffer = { handle, true };
        GL::bindBuffer(GL_ARRAY_BUFFER, handle);
        return false;
    }
    return true;
}

bool RenderState::indexBuffer(GLuint handle) {
    if (!m_indexBuffer.set || m_indexBuffer.handle != handle) {
        m_indexBuffer = { handle, true };
        GL::bindBuffer(GL_ELEMENT_ARRAY_BUFFER, handle);
        return false;
    }
    return true;
}

void RenderState::indexBufferUnset(GLuint handle) {
    if (m_indexBuffer.handle == handle) {
        m_indexBuffer.set = false;
    }
}

GLuint RenderState::getQuadIndexBuffer() {
    if (m_quadIndexBuffer == 0) {
        generateQuadIndexBuffer();
    }
    return m_quadIndexBuffer;
}

void RenderState::deleteQuadIndexBuffer() {
    indexBufferUnset(m_quadIndexBuffer);
    GL::deleteBuffers(1, &m_quadIndexBuffer);
    m_quadIndexBuffer = 0;
}

void RenderState::generateQuadIndexBuffer() {

    std::vector<GLushort> indices;
    indices.reserve(MAX_QUAD_VERTICES / 4 * 6);

    for (size_t i = 0; i < MAX_QUAD_VERTICES; i += 4) {
        indices.push_back(i + 2);
        indices.push_back(i + 0);
        indices.push_back(i + 1);
        indices.push_back(i + 1);
        indices.push_back(i + 3);
        indices.push_back(i + 2);
    }

    GL::genBuffers(1, &m_quadIndexBuffer);
    indexBuffer(m_quadIndexBuffer);
    GL::bufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLushort),
                   reinterpret_cast<GLbyte*>(indices.data()), GL_STATIC_DRAW);

}

bool RenderState::framebuffer(GLuint handle) {
    if (!m_framebuffer.set || m_framebuffer.handle != handle) {
        m_framebuffer = { handle, true };
        GL::bindFramebuffer(GL_FRAMEBUFFER, handle);
        return false;
    }
    return true;
}

bool RenderState::viewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (!m_viewport.set || m_viewport.x != x || m_viewport.y != y
      || m_viewport.width != width || m_viewport.height != height) {
        m_viewport = { x, y, width, height, true };
        GL::viewport(x, y, width, height);
        return false;
    }
    return true;
}

GLuint RenderState::defaultFrameBuffer() const {
    return (GLuint)m_defaultFramebuffer;
}

} // namespace Tangram
