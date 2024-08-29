#pragma once

#include "gl/uniform.h"

#include <memory>

namespace Tangram {

class RenderState;
class ShaderProgram;
class ShaderSource;
class StyledMesh;
class VertexLayout;
class View;

class SkyManager
{
public:
    SkyManager();
    void draw(RenderState& rs, View& _view);

private:
    void buildProgram();
    void setupUniforms(RenderState& rs, const View& _view);
    void buildMesh(float x0, float y0, float x1, float y1);

    std::unique_ptr<ShaderSource> m_shaderSource;
    std::unique_ptr<ShaderProgram> m_shaderProgram;
    std::shared_ptr<VertexLayout> m_vertexLayout;
    std::unique_ptr<StyledMesh> m_mesh;  // Mesh<SkyVertex>

    struct UniformBlock {
        UniformLocation uTime{"u_time"};
        UniformLocation uDevicePixelRatio{"u_device_pixel_ratio"};
        UniformLocation uResolution{"u_resolution"};

        UniformLocation uHorizonColor{"u_horizon_color"};
        UniformLocation uZenithColor{"u_zenith_color"};
    } m_uniforms;
};

}
