#pragma once

#include "gl/texture.h"
#include "gl/mesh.h"
#include "gl/shaderProgram.h"
#include "glm/vec2.hpp"
#include "util/util.h"
#include <string>
#include <deque>
#include <vector>
#include <mutex>
#include <cstdio>
#include <sstream>
#include <iomanip>

namespace Tangram {

#define LOG_CAPACITY        20
#define VERTEX_BUFFER_SIZE  99999

typedef int FontID;
class RenderState;

class TextDisplay {

public:

    static TextDisplay& Instance() {
        static TextDisplay instance;
        instance.init();
        return instance;
    }

    ~TextDisplay();

    void setMargins(glm::vec4 _margins) { m_margins = _margins; }  // TRBL

    void init();
    void deinit();

    /* Draw stacked messages added through log and draw _infos string list */
    void draw(RenderState& rs, const View& _view, const std::vector<std::string>& _infos);

    /* Stack the log message to be displayed in the screen log */
    void log(std::string s);  //const char* fmt, ...);

private:

    TextDisplay();

    void draw(RenderState& rs, const std::string& _text, int _posx, int _posy);

    glm::vec4 m_margins;  //glm::vec2 m_textDisplayResolution;
    bool m_initialized;
    std::unique_ptr<ShaderProgram> m_shader;
    std::deque<std::string> m_log;
    std::mutex m_mutex;
    char* m_vertexBuffer = nullptr;

    UniformLocation m_uOrthoProj{"u_orthoProj"};
    UniformLocation m_uColor{"u_color"};

};

#define LOGS(fmt, ...) \
do { Tangram::TextDisplay::Instance().log(fstring(fmt, ## __VA_ARGS__)); } while(0)

}
