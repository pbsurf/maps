#include "style/contourTextStyle.h"

#include "data/propertyItem.h"
#include "labels/textLabels.h"
#include "scene/drawRule.h"
#include "style/textStyleBuilder.h"
#include "tile/tile.h"
#include "util/elevationManager.h"
#include "log.h"

#include <glm/gtx/norm.hpp>

namespace Tangram {

class ContourTextStyleBuilder : public TextStyleBuilder {

public:

    ContourTextStyleBuilder(const TextStyle& _style)
        : TextStyleBuilder(_style) {}

    void setup(const Tile& _tile) override;

    void setup(const Marker& _marker, int zoom) override {
        LOGE("ContourTextStyle cannot be used with markers!");
    }

    bool addFeature(const Feature& _feat, const DrawRule& _rule) override;

    std::unique_ptr<StyledMesh> build() override;

private:
    double getContourLine(glm::vec2 pos, Line& line);

    TileID m_tileId = {-1, -1, -1};
    std::shared_ptr<Texture> m_texture;
    //bool metricUnits = true;
};


void ContourTextStyleBuilder::setup(const Tile& _tile) {

    // nothing to do if no elevation data
    if (_tile.rasters().empty() || _tile.rasters().front().texture->width() <= 1) { return; }

    m_tileId = _tile.getID();
    m_texture = _tile.rasters().front().texture;
    TextStyleBuilder::setup(_tile);
}

double ContourTextStyleBuilder::getContourLine(glm::vec2 pos, Line& line)  //Texture& tex,
{
    const float maxPosErr2 = 0.25f*0.25f;
    const float labelLen = 50/256.0f;
    const float stepSize = 2/256.0f;
    const size_t numLinePts = int(1.25f*labelLen/stepSize);
    // this obviously needs to match values in contour line shader
    double elevStep =  m_tileId.z >= 14 ? 100. : m_tileId.z >= 12 ? 200. : 500.;

    while (1) {
        double level = NAN;
        glm::vec2 dpos, grad;  //, prevTangent;
        int niter = 0;
        do {
            double elev = ElevationManager::elevationLerp(*m_texture, pos, &grad);
            if (std::isnan(level)) {
                level = std::round(elev/elevStep)*elevStep;
            }
            dpos = glm::vec2(level - elev)/grad;
            pos += dpos;

            // abort if outside tile or too many iterations
            if(++niter > 10 || pos.x < 0 || pos.y < 0 || pos.x > 1 || pos.y > 1) { return NAN; }

        } while (glm::length2(dpos) > maxPosErr2);

        line.push_back(pos);
        if(line.size() >= numLinePts) { return level; }
        glm::vec2 tangent = glm::normalize(glm::vec2(-grad.y, grad.x));
        //if(glm::dot(tangent, prevTangent ... check for excess angle
        pos += tangent*stepSize;
    }
}

bool ContourTextStyleBuilder::addFeature(const Feature& _feat, const DrawRule& _rule) {

    if (!m_texture || !checkRule(_rule)) { return false; }

    Properties props = {{{"name", "dummy"}}};  // applyRule() will fail if name is empty
    TextStyle::Parameters params = applyRule(_rule, props, false);  //_feat.props
    if (!params.font) { return false; }

    // Keep start position of new quads
    size_t quadsStart = m_quads.size(), numLabels = m_labels.size();

    int ngrid = 2;
    glm::vec2 pos;
    for (int col = 0; col < ngrid; col++) {
        pos.y = (col + 0.5f)/ngrid;
        for (int row = 0; row < ngrid; row++) {
            pos.x = (row + 0.5f)/ngrid;

            Line line;
            double level = getContourLine(pos, line);
            if (std::isnan(level)) { continue; }

            LabelAttributes attrib;
            params.text = std::to_string(int(level));  //metricUnits ? level : level*3.28084));
            if (!prepareLabel(params, Label::Type::line, attrib)) { return false; }
            addCurvedTextLabels(line, params, attrib, _rule);
        }
    }


    if (numLabels == m_labels.size()) { m_quads.resize(quadsStart); }
    return true;
}

std::unique_ptr<StyledMesh> ContourTextStyleBuilder::build()
{
  m_texture.reset();
  return TextStyleBuilder::build();
}

std::unique_ptr<StyleBuilder> ContourTextStyle::createBuilder() const {
    return std::make_unique<ContourTextStyleBuilder>(*this);
}

}
