#pragma once

#include <string>

namespace Tangram {

class RenderState;
class Map;
class View;

struct FrameInfo {

    static void beginUpdate();
    static void beginFrame();

    static void endUpdate();

    static void begin(const std::string& tag);
    static void end(const std::string& tag);

    static void draw(RenderState& rs, const View& _view, Map& _map);

    struct scope {
        std::string tag;
        scope(const std::string& _tag) : tag(_tag) { begin(tag); }
        ~scope() { end(tag); }
    };
};

}
