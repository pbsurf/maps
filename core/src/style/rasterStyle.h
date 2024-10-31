#pragma once

#include "style/polygonStyle.h"

namespace Tangram {

// derive from PolygonStyle just to avoid duplicating polygon shader source
class RasterStyle : public PolygonStyle {

public:

    RasterStyle(std::string _name, Blending _blendMode = Blending::opaque);
    void build(const Scene& _scene) override;
    void constructVertexLayout() override;
    void constructShaderProgram() override;
    std::unique_ptr<StyleBuilder> createBuilder() const override;

    StyledMesh* rasterMesh() const { return m_rasterMesh.get(); }

protected:
    std::unique_ptr<StyledMesh> m_rasterMesh;

    UniformLocation m_uColor{"u_color"};
    UniformLocation m_uOrder{"u_order"};
    friend struct SharedMesh;

};

}
