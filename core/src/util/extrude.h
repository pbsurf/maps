#pragma once

#include "data/properties.h"
#include "scene/styleParam.h"
#include "glm/vec2.hpp"

namespace YAML {
    class Node;
}

namespace Tangram {

// Returns a Value to represent the extrusion option specified in the node, one of:
//  TextSource w/ height property name(s), a single number, or a sequence of two numbers.
StyleParam::Value parseExtrudeNode(const YAML::Node& node);

// Returns the lower or upper extrusion values for a given Extrude and set of feature properties
float getLowerExtrudeMeters(const StyleParam& _extrude, const Properties& _props);
float getUpperExtrudeMeters(const StyleParam& _extrude, const Properties& _props);

}
