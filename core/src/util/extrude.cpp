#include "util/extrude.h"
#include "log.h"
#include "util/yamlUtil.h"
#include <cmath>

namespace Tangram {

StyleParam::Value parseExtrudeNode(const YAML::Node& node) {
    // Values specified from the stylesheet are assumed to be meters with no unit suffix
    float first = 0, second = 0;

    if (node.IsSequence() && node.size() == 2) {
        if (YamlUtil::getFloat(node[0], first) && YamlUtil::getFloat(node[1], second)) {
            // Got two numbers, so return an extrusion from first to second
            return glm::vec2(first, second);
        }

        if (node[0].IsScalar() && node[1].IsScalar()) {
            // assume property names for min and max heights
            return StyleParam::TextSource({node[0].Scalar(), node[1].Scalar()});
        }
    }

    if (node.IsScalar()) {
        bool extrudeBoolean = false;
        if (YamlUtil::getBool(node, extrudeBoolean)) {
            if (extrudeBoolean) {
                return StyleParam::TextSource({"min_height", "height"});
            }
            // "false" means perform no extrusion
            return glm::vec2(0, 0);
        }

        if (YamlUtil::getFloat(node, first)) {
            // No second number, so return an extrusion from 0 to the first number
            return glm::vec2(0, first);
        }

        // assume single height property name
        return StyleParam::TextSource({node.Scalar()});
    }
    // No numbers found, return zero extrusion
    LOGE("Invalid extrude property: %s", Dump(node).c_str());
    return glm::vec2(0, 0);
}

float getLowerExtrudeMeters(const StyleParam& _extrude, const Properties& _props) {

    double lower = 0;
    if (_extrude.value.is<StyleParam::TextSource>()) {
        const auto& keys = _extrude.value.get<StyleParam::TextSource>().keys;
        if (keys.size() > 1) {
            _props.getNumber(keys.front(), lower);
        }
    } else if (_extrude.value.is<glm::vec2>()) {
        lower = _extrude.value.get<glm::vec2>()[0];
    }
    return lower;
}

float getUpperExtrudeMeters(const StyleParam& _extrude, const Properties& _props) {

    double upper = 0;
    if (_extrude.value.is<StyleParam::TextSource>()) {
        const auto& keys = _extrude.value.get<StyleParam::TextSource>().keys;
        if (!keys.empty()) {
            _props.getNumber(keys.back(), upper);
        }
    } else if (_extrude.value.is<glm::vec2>()) {
        upper = _extrude.value.get<glm::vec2>()[1];
    }
    return upper;
}

}
