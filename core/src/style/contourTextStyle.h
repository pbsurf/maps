#pragma once

#include "style/textStyle.h"

//#define TANGRAM_CONTOUR_DEBUG
//#include "style/debugStyle.h"

namespace Tangram {

#ifdef TANGRAM_CONTOUR_DEBUG
class ContourTextStyle : public DebugStyle {

public:
    ContourTextStyle(std::string _name, bool _sdf = true) : DebugStyle(_name, Blending::overlay, GL_LINES) {}
#else
class ContourTextStyle : public TextStyle {

public:
    ContourTextStyle(std::string _name, bool _sdf = true) : TextStyle(_name, _sdf) {}
#endif
    void build(const Scene& _scene) override;
    std::unique_ptr<StyleBuilder> createBuilder() const override;

    bool m_metricUnits = true;
};

}
