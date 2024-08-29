#include "util/skyManager.h"
#include "gl/mesh.h"
#include "gl/renderState.h"
#include "gl/shaderProgram.h"
#include "view/view.h"

// probably should move these to separate files in core/shaders

static const char* sky_vs = R"RAW_GLSL(
//#pragma tangram: extensions

#ifdef GL_ES
precision mediump float;
#endif

//#pragma tangram: defines
//#pragma tangram: uniforms

attribute vec4 a_position;

varying vec4 v_position;

//#pragma tangram: global

void main() {
    v_position = a_position;
    gl_Position = a_position;
}
)RAW_GLSL";

static const char* sky_fs = R"RAW_GLSL(
//#pragma tangram: extensions

#ifdef GL_ES
precision highp float;
#endif

#pragma tangram: defines

uniform vec4 u_horizon_color;
uniform vec4 u_zenith_color;

#pragma tangram: uniforms

varying vec4 v_position;

//#pragma tangram: material
//#pragma tangram: lighting
//#pragma tangram: raster
#pragma tangram: global

void main(void) {

    #pragma tangram: setup

    #pragma tangram: color

    vec4 color = mix(horizon_color, zenith_color, v_position.y);

    #pragma tangram: filter

    gl_FragColor = color;
}
)RAW_GLSL";

namespace Tangram {

struct SkyVertex {
    SkyVertex(float x, float y) : position(x, y) {}
    glm::vec2 position;
};


SkyManager::SkyManager() : m_shaderSource(std::make_unique<ShaderSource>()) {}

void SkyManager::setupUniforms(RenderState& rs, const View& _view)
{
    m_shaderProgram->setUniformf(rs, m_uniforms.uTime, rs.frameTime());
    m_shaderProgram->setUniformf(rs, m_uniforms.uDevicePixelRatio, _view.pixelScale());
    m_shaderProgram->setUniformf(rs, m_uniforms.uResolution, _view.getWidth(), _view.getHeight());

    // probably want to use something like StyleUniform for these
    m_shaderProgram->setUniformf(rs, m_uniforms.uHorizonColor, glm::vec4(0.5, 0.5, 1.0, 1.0));  //m_horizonColor);
    m_shaderProgram->setUniformf(rs, m_uniforms.uZenithColor, glm::vec4(0.0, 0.0, 1.0, 1.0));  //m_zenithColor);
}

void SkyManager::buildProgram()
{
    m_vertexLayout = std::shared_ptr<VertexLayout>(new VertexLayout({
        {"a_position", 2, GL_FLOAT, false, 0},
    }));

    m_shaderSource->setSourceStrings(sky_fs, sky_vs);

    // add defines ...
    //m_shaderSource->addSourceBlock("defines", "#define TANGRAM_TERRAIN_3D\n", false);

    std::string vertSrc = m_shaderSource->buildVertexSource();
    std::string fragSrc = m_shaderSource->buildFragmentSource();

    m_shaderProgram = std::make_unique<ShaderProgram>(vertSrc, fragSrc, m_vertexLayout.get());
    m_shaderProgram->setDescription("SkyManager");

    m_shaderSource.reset();
}

void SkyManager::buildMesh(float x0, float y0, float x1, float y1)
{
    MeshData<SkyVertex> meshData;

    meshData.vertices.push_back({x0, y0});
    meshData.vertices.push_back({x0, y1});
    meshData.vertices.push_back({x1, y0});
    meshData.vertices.push_back({x1, y1});

    meshData.offsets.emplace_back(meshData.indices.size(), meshData.vertices.size());
    // Create mesh from vertices.
    auto mesh = std::make_unique<Mesh<SkyVertex>>(m_vertexLayout, GL_TRIANGLE_STRIP);
    mesh->compile(meshData);
    m_mesh = std::move(mesh);
    //m_meshData.clear();
}


void SkyManager::draw(RenderState& rs, View& _view)
{
    float horizon = _view.horizonScreenPosition();
    if(horizon < 0 || horizon > _view.getHeight()) { return; }
    buildMesh(0, horizon, _view.getWidth(), _view.getHeight());

    if (!m_mesh) { return; }
    if (!m_shaderProgram) { buildProgram(); }

    setupUniforms(rs, _view);

    rs.blending(GL_FALSE);
    //rs.blendingFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    rs.depthTest(GL_FALSE);
    rs.depthMask(GL_FALSE);

    m_mesh->draw(rs, *m_shaderProgram);
}

}
