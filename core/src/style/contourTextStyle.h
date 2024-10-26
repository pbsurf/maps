#pragma once

#include "style/textStyle.h"

#ifndef NDEBUG
#define TANGRAM_CONTOUR_DEBUG
#include "style/debugStyle.h"
#endif

namespace Tangram {

class ContourTextStyle : public TextStyle {

public:
    ContourTextStyle(std::string _name, bool _sdf = true) : TextStyle(_name, _sdf) {}
    void build(const Scene& _scene) override;
    std::unique_ptr<StyleBuilder> createBuilder() const override;

    bool m_metricUnits = true;
};

#ifdef TANGRAM_CONTOUR_DEBUG
class ContourDebugStyle : public DebugStyle {

public:
    ContourDebugStyle(std::string _name) : DebugStyle(_name, Blending::inlay, GL_LINES) {}
    void build(const Scene& _scene) override;
    std::unique_ptr<StyleBuilder> createBuilder() const override;

    bool m_metricUnits = true;
    bool m_terrain3d = false;
};
#endif

}
