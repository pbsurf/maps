#include "gl/shaderProgram.h"

#include "gl/glError.h"
#include "gl/renderState.h"
#include "gl/vertexLayout.h"
#include "glm/gtc/type_ptr.hpp"
#include "scene/light.h"
#include "log.h"
#include "platform.h"

#include <sstream>

namespace Tangram {

ShaderProgram::ShaderProgram(const std::string& _vertSrc,
                             const std::string& _fragSrc,
                             VertexLayout* _layout)
    : m_vertexLayout(_layout), m_fragmentShaderSource(_fragSrc), m_vertexShaderSource(_vertSrc) { }

ShaderProgram::~ShaderProgram() {
    if (m_rs) {
        if (m_glProgram) {
            // Delete only the program, separate shaders are cached and eventually deleted by RenderState.
            // TODO: This approach leaves shaders in memory even if they aren't used by any programs until
            // the entire Map (and its RenderState) is destroyed. Ideally we could use a reference-counted
            // cache to keep shaders in memory only while at least one program uses them. (MEB 2018/5/17)
            m_rs->queueProgramDeletion(m_glProgram);
        }
    }
}

GLint ShaderProgram::getUniformLocation(const UniformLocation& _uniform) {

    if (_uniform.location == -2) {
        _uniform.location = GL::getUniformLocation(m_glProgram, _uniform.name.c_str());
    }
    return _uniform.location;
}

bool ShaderProgram::use(RenderState& rs) {

    if (m_needsBuild) {
        m_needsBuild = false;
        build(rs);
    }

    if (isValid()) {
        rs.shaderProgram(m_glProgram);
        return true;
    }

    return false;
}

bool ShaderProgram::build(RenderState& rs) {

    auto& vertSrc = m_vertexShaderSource;
    auto& fragSrc = m_fragmentShaderSource;

    // Compile vertex and fragment shaders
    GLuint vertexShader = makeCompiledShader(rs, vertSrc, GL_VERTEX_SHADER);
    if (vertexShader == 0) {
        LOGE("Shader compilation failed for %s", m_description.c_str());
        return false;
    }

    GLuint fragmentShader = makeCompiledShader(rs, fragSrc, GL_FRAGMENT_SHADER);
    if (fragmentShader == 0) {
        LOGE("Shader compilation failed for %s", m_description.c_str());
        return false;
    }

    // Attrib locations must be set before program is linked
    GLuint program = GL::createProgram();
    auto& attribs = m_vertexLayout->getAttribs();
    for (size_t ii = 0; ii < attribs.size(); ++ii) {
        GL::bindAttribLocation(program, GLuint(ii), attribs[ii].name.c_str());
    }

    // Link shaders into a program
    program = makeLinkedShaderProgram(program, fragmentShader, vertexShader);
    if (program == 0) {
        LOGE("Shader compilation failed for %s", m_description.c_str());
        return false;
    }

    m_glProgram = program;
    m_glFragmentShader = fragmentShader;
    m_glVertexShader = vertexShader;
    m_rs = &rs;

    return true;
}

GLuint ShaderProgram::makeLinkedShaderProgram(GLuint program, GLuint _fragShader, GLuint _vertShader) {

    GL::attachShader(program, _fragShader);
    GL::attachShader(program, _vertShader);
    GL::linkProgram(program);

    GLint isLinked;
    GL::getProgramiv(program, GL_LINK_STATUS, &isLinked);

    if (isLinked == GL_FALSE) {
        GLint infoLength = 0;
        GL::getProgramiv(program, GL_INFO_LOG_LENGTH, &infoLength);

        if (infoLength > 1) {
            std::vector<GLchar> infoLog(infoLength);
            GL::getProgramInfoLog(program, infoLength, NULL, &infoLog[0]);
            LOGE("linking program:\n%s", &infoLog[0]);
        }

        GL::deleteProgram(program);
        return 0;
    }

    return program;
}

GLuint ShaderProgram::makeCompiledShader(RenderState& rs, const std::string& _src, GLenum _type) {

    auto& cache = (_type == GL_VERTEX_SHADER) ? rs.vertexShaders : rs.fragmentShaders;

    auto entry = cache.emplace(_src, 0);
    if (!entry.second) {
        return entry.first->second;
    }

    GLuint shader = GL::createShader(_type);

    const GLchar* source = (const GLchar*) _src.c_str();
    GL::shaderSource(shader, 1, &source, NULL);
    GL::compileShader(shader);

    GLint isCompiled;
    GL::getShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);

    if (isCompiled == GL_FALSE) {
        GLint infoLength = 0;
        GL::getShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLength);

        if (infoLength > 1) {
            std::string infoLog;
            infoLog.resize(infoLength);

            GL::getShaderInfoLog(shader, infoLength, NULL, static_cast<GLchar*>(&infoLog[0]));
            LOGE("Shader compilation failed\n%s", infoLog.c_str());

            std::stringstream sourceStream(source);
            std::string item;
            std::vector<std::string> sourceLines;
            while (std::getline(sourceStream, item)) { sourceLines.push_back(item); }

            // Print errors with context
            std::string line;
            std::stringstream logStream(infoLog);

            while (std::getline(logStream, line)) {
                if (line.length() < 2) { continue; }

                int lineNum = 0;
                if (!sscanf(line.c_str(), "%*d%*[(:]%d", &lineNum)) { continue; }
                LOGE("\nError on line %d: %s", lineNum, line.c_str());

                for (int i = std::max(0, lineNum-5); i < lineNum+5; i++) {
                    if (size_t(i) >= sourceLines.size()) { break; }
                    LOGE("%d: %s", i+1, sourceLines[i].c_str());
                }
            }

            // Print full source with line numbers
            LOGD("\n\n");
            for (size_t i = 0; i < sourceLines.size(); i++) {
                LOGD("%d: %s", i+1, sourceLines[i].c_str());
            }
        }

        GL::deleteShader(shader);
        return 0;
    }

    entry.first->second = shader;

    return shader;
}

void ShaderProgram::setUniformi(RenderState& rs, const UniformLocation& _loc, int _value) {
    if (!use(rs)) { return; }
    GLint location = getUniformLocation(_loc);
    if (location >= 0) {
        bool cached = getFromCache(location, _value);
        if (!cached) { GL::uniform1i(location, _value); }
    }
}

void ShaderProgram::setUniformi(RenderState& rs, const UniformLocation& _loc, int _value0, int _value1) {
    if (!use(rs)) { return; }
    GLint location = getUniformLocation(_loc);
    if (location >= 0) {
        bool cached = getFromCache(location, glm::vec2(_value0, _value1));
        if (!cached) { GL::uniform2i(location, _value0, _value1); }
    }
}

void ShaderProgram::setUniformi(RenderState& rs, const UniformLocation& _loc, int _value0, int _value1, int _value2) {
    if (!use(rs)) { return; }
    GLint location = getUniformLocation(_loc);
    if (location >= 0) {
        bool cached = getFromCache(location, glm::vec3(_value0, _value1, _value2));
        if (!cached) { GL::uniform3i(location, _value0, _value1, _value2); }
    }
}

void ShaderProgram::setUniformi(RenderState& rs, const UniformLocation& _loc, int _value0, int _value1, int _value2, int _value3) {
    if (!use(rs)) { return; }
    GLint location = getUniformLocation(_loc);
    if (location >= 0) {
        bool cached = getFromCache(location, glm::vec4(_value0, _value1, _value2, _value3));
        if (!cached) { GL::uniform4i(location, _value0, _value1, _value2, _value3); }
    }
}

void ShaderProgram::setUniformf(RenderState& rs, const UniformLocation& _loc, float _value) {
    if (!use(rs)) { return; }
    GLint location = getUniformLocation(_loc);
    if (location >= 0) {
        bool cached = getFromCache(location, _value);
        if (!cached) { GL::uniform1f(location, _value); }
    }
}

void ShaderProgram::setUniformf(RenderState& rs, const UniformLocation& _loc, float _value0, float _value1) {
    setUniformf(rs, _loc, glm::vec2(_value0, _value1));
}

void ShaderProgram::setUniformf(RenderState& rs, const UniformLocation& _loc, float _value0, float _value1, float _value2) {
    setUniformf(rs, _loc, glm::vec3(_value0, _value1, _value2));
}

void ShaderProgram::setUniformf(RenderState& rs, const UniformLocation& _loc, float _value0, float _value1, float _value2, float _value3) {
    setUniformf(rs, _loc, glm::vec4(_value0, _value1, _value2, _value3));
}

void ShaderProgram::setUniformf(RenderState& rs, const UniformLocation& _loc, const glm::vec2& _value) {
    if (!use(rs)) { return; }
    GLint location = getUniformLocation(_loc);
    if (location >= 0) {
        bool cached = getFromCache(location, _value);
        if (!cached) { GL::uniform2f(location, _value.x, _value.y); }
    }
}

void ShaderProgram::setUniformf(RenderState& rs, const UniformLocation& _loc, const glm::vec3& _value) {
    if (!use(rs)) { return; }
    GLint location = getUniformLocation(_loc);
    if (location >= 0) {
        bool cached = getFromCache(location, _value);
        if (!cached) { GL::uniform3f(location, _value.x, _value.y, _value.z); }
    }
}

void ShaderProgram::setUniformf(RenderState& rs, const UniformLocation& _loc, const glm::vec4& _value) {
    if (!use(rs)) { return; }
    GLint location = getUniformLocation(_loc);
    if (location >= 0) {
        bool cached = getFromCache(location, _value);
        if (!cached) { GL::uniform4f(location, _value.x, _value.y, _value.z, _value.w); }
    }
}

void ShaderProgram::setUniformMatrix2f(RenderState& rs, const UniformLocation& _loc, const glm::mat2& _value, bool _transpose) {
    if (!use(rs)) { return; }
    GLint location = getUniformLocation(_loc);
    if (location >= 0) {
        bool cached = !_transpose && getFromCache(location, _value);
        if (!cached) { GL::uniformMatrix2fv(location, 1, _transpose, glm::value_ptr(_value)); }
    }
}

void ShaderProgram::setUniformMatrix3f(RenderState& rs, const UniformLocation& _loc, const glm::mat3& _value, bool _transpose) {
    if (!use(rs)) { return; }
    GLint location = getUniformLocation(_loc);
    if (location >= 0) {
        bool cached = !_transpose && getFromCache(location, _value);
        if (!cached) { GL::uniformMatrix3fv(location, 1, _transpose, glm::value_ptr(_value)); }
    }
}

void ShaderProgram::setUniformMatrix4f(RenderState& rs, const UniformLocation& _loc, const glm::mat4& _value, bool _transpose) {
    if (!use(rs)) { return; }
    GLint location = getUniformLocation(_loc);
    if (location >= 0) {
        bool cached = !_transpose && getFromCache(location, _value);
        if (!cached) { GL::uniformMatrix4fv(location, 1, _transpose, glm::value_ptr(_value)); }
    }
}

void ShaderProgram::setUniformf(RenderState& rs, const UniformLocation& _loc, const UniformArray1f& _value) {
    if (!use(rs)) { return; }
    GLint location = getUniformLocation(_loc);
    if (location >= 0) {
        bool cached = getFromCache(location, _value);
        if (!cached) { GL::uniform1fv(location, _value.size(), _value.data()); }
    }
}

void ShaderProgram::setUniformf(RenderState& rs, const UniformLocation& _loc, const UniformArray2f& _value) {
    if (!use(rs)) { return; }
    GLint location = getUniformLocation(_loc);
    if (location >= 0) {
        bool cached = getFromCache(location, _value);
        if (!cached) { GL::uniform2fv(location, _value.size(), (float*)_value.data()); }
    }
}

void ShaderProgram::setUniformf(RenderState& rs, const UniformLocation& _loc, const UniformArray3f& _value) {
    if (!use(rs)) { return; }
    GLint location = getUniformLocation(_loc);
    if (location >= 0) {
        bool cached = getFromCache(location, _value);
        if (!cached) { GL::uniform3fv(location, _value.size(), (float*)_value.data()); }
    }
}

void ShaderProgram::setUniformi(RenderState& rs, const UniformLocation& _loc, const UniformTextureArray& _value) {
    if (!use(rs)) { return; }
    GLint location = getUniformLocation(_loc);
    if (location >= 0) {
        bool cached = getFromCache(location, _value);
        if (!cached) { GL::uniform1iv(location, _value.slots.size(), _value.slots.data()); }
    }
}

}
