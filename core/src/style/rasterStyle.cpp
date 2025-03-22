#include "style/rasterStyle.h"

#include "scene/scene.h"
#include "gl/mesh.h"
#include "gl/shaderProgram.h"
#include "log.h"

constexpr float position_scale = 8192.0f;

namespace Tangram {

struct SharedMesh : public StyledMesh
{
    StyledMesh* m_mesh;
    const RasterStyle* m_style;
    float m_order;
    glm::vec4 m_color;

    SharedMesh(StyledMesh* _mesh, const RasterStyle* _style, float _order, ColorF _color)
        : m_mesh(_mesh), m_style(_style), m_order(_order), m_color(_color.r, _color.g, _color.b, _color.a) {}

    bool draw(RenderState& rs, ShaderProgram& _shader, bool _useVao = true) override {
        _shader.setUniformf(rs, m_style->m_uColor, m_color);
        _shader.setUniformf(rs, m_style->m_uOrder, m_order);
        return m_mesh->draw(rs, _shader, _useVao);
    }

    size_t bufferSize() const override { return 0; }
};


struct RasterVertex {
    RasterVertex(glm::vec2 position)
        : pos(glm::i16vec2{ nearbyint(position * position_scale) }) {}

    glm::i16vec2 pos;
};

RasterStyle::RasterStyle(std::string _name, Blending _blendMode)
    : PolygonStyle(_name, _blendMode, GL_TRIANGLES, false) {
    m_type = StyleType::raster;
    m_rasterType = RasterType::color;
}

void RasterStyle::constructVertexLayout() {
    m_vertexLayout = std::shared_ptr<VertexLayout>(new VertexLayout({
        {"a_position", 2, GL_SHORT, false, 0},
    }));
}

void RasterStyle::constructShaderProgram() {
    //m_shaderSource->setSourceStrings(polygon_fs, polygon_vs);
    m_texCoordsGeneration = false;
    PolygonStyle::constructShaderProgram();
    m_shaderSource->addSourceBlock("defines", "#define TANGRAM_RASTER_STYLE\n");
}

void RasterStyle::build(const Scene& _scene) {
    Style::build(_scene);

    uint32_t resolution = _scene.elevationManager() ? 64 : 1;
    float elementSize = 1.f / resolution;
    uint16_t index = 0;
    MeshData<RasterVertex> meshData;
    meshData.vertices.reserve((resolution+1) * (resolution+1));
    meshData.indices.reserve(6*resolution*resolution);
    for (uint32_t col = 0; col <= resolution; col++) {
        float y = col * elementSize;
        for (uint32_t row = 0; row <= resolution; row++) {
            float x = row * elementSize;
            meshData.vertices.push_back({{x, y}});

            if (row < resolution && col < resolution) {
                meshData.indices.push_back(index);
                meshData.indices.push_back(index + 1);
                meshData.indices.push_back(index + resolution + 1);

                meshData.indices.push_back(index + 1);
                meshData.indices.push_back(index + resolution + 2);
                meshData.indices.push_back(index + resolution + 1);
            }
            index++;
        }
    }
    meshData.offsets.emplace_back(meshData.indices.size(), meshData.vertices.size());
    // Create mesh from vertices.
    auto rasterMesh = std::make_unique<Mesh<RasterVertex>>(vertexLayout(), drawMode());
    rasterMesh->compile(meshData);
    m_rasterMesh = std::move(rasterMesh);
}


struct RasterStyleBuilder : public StyleBuilder {

public:

    struct Parameters {
        uint32_t order = 0;
        Color color = 0xffffffff;
    };

    RasterStyleBuilder(const RasterStyle& _style) : m_style(_style) {}

    void setup(const Tile& _tile) override {
        m_zoom = _tile.getID().z;
    }

    void setup(const Marker& _marker, int zoom) override {
        LOGE("RasterStyle cannot be used with markers!");
    }

    bool addFeature(const Feature& _feat, const DrawRule& _rule) override;
    const Style& style() const override { return m_style; }
    std::unique_ptr<StyledMesh> build() override { return std::move(m_mesh); }
    Parameters parseRule(const DrawRule& _rule, const Properties& _props);

private:

    const RasterStyle& m_style;
    std::unique_ptr<StyledMesh> m_mesh;
    int m_zoom = 0;
};

auto RasterStyleBuilder::parseRule(const DrawRule& _rule, const Properties& _props) -> Parameters {
    Parameters p;
    _rule.get(StyleParamKey::color, p.color.abgr);
    float alpha = 1;
    if (_rule.get(StyleParamKey::alpha, alpha)) {
        p.color = p.color.withAlpha(alpha);
    }

    _rule.get(StyleParamKey::order, p.order);

    if (Tangram::getDebugFlag(Tangram::DebugFlags::proxy_colors)) {
        p.color.abgr <<= (m_zoom % 6);
    }

    return p;
}

bool RasterStyleBuilder::addFeature(const Feature& _feat, const DrawRule& _rule) {

    if (!checkRule(_rule)) { return false; }

    if (_feat.geometryType != GeometryType::polygons || _feat.polygons.size() != 1) {
        LOGE("Invalid geometry passed to RasterStyle");
        return false;
    }
    if (m_mesh) {
        LOGE("Only one Raster feature can be added per tile!");
        return false;
    }

    auto p = parseRule(_rule, _feat.props);
    m_mesh = std::make_unique<SharedMesh>(m_style.rasterMesh(), &m_style, p.order, p.color.toColorF());
    return true;
}

std::unique_ptr<StyleBuilder> RasterStyle::createBuilder() const {
    return std::make_unique<RasterStyleBuilder>(*this);
}

}
