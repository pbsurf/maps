#pragma once

#include "style/textStyle.h"

namespace Tangram {

class ContourTextStyle : public TextStyle {

public:
    ContourTextStyle(std::string _name, bool _sdf = true) : TextStyle(_name, _sdf) {}

    std::unique_ptr<StyleBuilder> createBuilder() const override;

};

}
