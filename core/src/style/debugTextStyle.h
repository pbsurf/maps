#pragma once

#include "style/textStyle.h"

namespace Tangram {

class DebugTextStyle : public TextStyle {

public:
    DebugTextStyle(std::string _name, bool _sdf = false) : TextStyle(_name, _sdf) { m_blendOrder = INT_MAX; }

    std::unique_ptr<StyleBuilder> createBuilder() const override;

};

}
