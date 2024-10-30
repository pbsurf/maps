#include "util/elevationManager.h"
#include "util/asyncWorker.h"
#include "data/rasterSource.h"
#include "style/rasterStyle.h"
#include "gl/shaderSource.h"
#include "gl/framebuffer.h"
#include "gl/renderState.h"
#include "view/view.h"
#include "marker/marker.h"
#include "log.h"
#include "debug/frameInfo.h"

// with depth test enabled and no blending, final value of output should be correct depth (if larger depth
//  written first, will be overwritten; if smaller depth written first, depth test will discard larger depth)
const static char* terrain_depth_fs = R"RAW_GLSL(#version 300 es
#ifdef GL_ES
precision highp float;
#endif

layout (location = 0) out highp uint depthOut;

void main(void) {
  depthOut = floatBitsToUint(gl_FragCoord.w);  //gl_FragCoord.z);
}
)RAW_GLSL";

#include "polygon_vs.h"

#include "rasters_glsl.h"
#include "scene/scene.h"
#include "gl/shaderProgram.h"
#include "gl/hardware.h"

#include "../../platforms/common/platform_gl.h"

namespace Tangram {

// smaller target greatly improves FPS
static float bufferScale = 2;
std::unique_ptr<RenderState> ElevationManager::m_renderState;
std::unique_ptr<AsyncWorker> ElevationManager::offscreenWorker;

class TerrainStyle : public RasterStyle
{
public:
  using RasterStyle::RasterStyle;

  void build(const Scene& _scene) override {
      RasterStyle::build(_scene);
      m_shaderProgram = std::make_shared<ShaderProgram>(
          m_shaderProgram->vertexShaderSource(), terrain_depth_fs, vertexLayout().get());
  }

  bool draw(RenderState& rs, const Tile& _tile) override {

      // need to check for mesh of cloned style to determine if we should draw tile
      auto& styleMesh = _tile.getMesh(*this);
      if (!styleMesh) { return false; }

      bool styleMeshDrawn = true;
      int prevTexUnit = rs.currentTextureUnit();
      setupTileShaderUniforms(rs, _tile, *m_shaderProgram, m_mainUniforms);
      m_shaderProgram->setUniformf(rs, m_uOrder, 0.f);

      if (!rasterMesh()->draw(rs, *m_shaderProgram)) {
          LOGN("Mesh built by style %s cannot be drawn", m_name.c_str());
          styleMeshDrawn = false;
      }

      rs.resetTextureUnit(prevTexUnit);
      return styleMeshDrawn;
  }
};

static double readElevTex(const Texture& tex, int x, int y)
{
  // see getElevation() in hillshade.yaml and https://github.com/tilezen/joerd
  if(tex.getOptions().pixelFormat == PixelFormat::FLOAT)
    return ((float*)tex.bufferData())[y*tex.width() + x];
  GLubyte* p = tex.bufferData() + y*tex.width()*4 + x*4;
  //(red * 256 + green + blue / 256) - 32768
  return (p[0]*256 + p[1] + p[2]/256.0) - 32768;
}

double ElevationManager::elevationLerp(const Texture& tex, glm::vec2 pos, glm::vec2* gradOut)
{
  double x0 = pos.x*tex.width() - 0.5, y0 = pos.y*tex.height() - 0.5;  // -0.5 to adjust for pixel centers
  // we should extrapolate at edges instead of clamping - see shader in raster_contour.yaml
  int ix0 = std::max(0, int(std::floor(x0)));
  int iy0 = std::max(0, int(std::floor(y0)));
  int ix1 = std::min(int(std::ceil(x0)), tex.width()-1);
  int iy1 = std::min(int(std::ceil(y0)), tex.height()-1);
  double fx = x0 - ix0, fy = y0 - iy0;
  double t00 = readElevTex(tex, ix0, iy0);
  double t01 = readElevTex(tex, ix0, iy1);
  double t10 = readElevTex(tex, ix1, iy0);
  double t11 = readElevTex(tex, ix1, iy1);

  if(gradOut) {
    double dx0 = t10 - t00, dx1 = t11 - t01;
    double dy0 = t01 - t00, dy1 = t11 - t10;
    gradOut->x = (dx0 + fy*(dx1 - dx0))*tex.width();
    gradOut->y = (dy0 + fx*(dy1 - dy0))*tex.height();
  }

  double t0 = t00 + fx*(t10 - t00);
  double t1 = t01 + fx*(t11 - t01);
  return t0 + fy*(t1 - t0);
}

double ElevationManager::elevationLerp(const Texture& tex, TileID tileId, ProjectedMeters meters)
{
  double scale = MapProjection::metersPerTileAtZoom(tileId.z);
  ProjectedMeters tileOrigin = MapProjection::tileSouthWestCorner(tileId);
  //ProjectedMeters meters = MapProjection::lngLatToProjectedMeters(pos);  //glm::dvec2(tileCoord) * scale + tileOrigin;
  ProjectedMeters offset = meters - tileOrigin;
  double ox = offset.x/scale, oy = offset.y/scale;
  if(ox < 0 || ox > 1 || oy < 0 || oy > 1)
    return 0;  //LOGE("Elevation tile position out of range");
  return elevationLerp(tex, glm::vec2(ox, oy));
}

//static TileID lngLatTile(LngLat ll, int z)
//{
//  int x = int(floor((ll.longitude + 180.0) / 360.0 * (1 << z)));
//  double latrad = ll.latitude * M_PI/180.0;
//  int y = int(floor((1.0 - asinh(tan(latrad)) / M_PI) / 2.0 * (1 << z)));
//  return TileID(x, y, z);
//}

static TileID projMetersTile(ProjectedMeters ll, int z)
{
  constexpr double hc = MapProjection::EARTH_HALF_CIRCUMFERENCE_METERS;
  double metersPerTile = MapProjection::metersPerTileAtZoom(z);
  return TileID(int((ll.x + hc)/metersPerTile), int((hc - ll.y)/metersPerTile), z);
}

double ElevationManager::getElevation(ProjectedMeters pos, bool& ok, bool ascend)
{
  static std::weak_ptr<Texture> prevTex;
  static TileID prevTileId = {0, 0, 0, 0};

  //constexpr double hc = MapProjection::EARTH_HALF_CIRCUMFERENCE_METERS;
  //double metersPerTile = MapProjection::metersPerTileAtZoom(m_currZoom);
  //double tile_x = (pos.x + hc)/metersPerTile;
  //double tile_y = (hc - pos.y)/metersPerTile;


  ok = true;
  //TileID llId = lngLatTile(MapProjection::projectedMetersToLngLat(pos), m_currZoom);
  TileID tileId = projMetersTile(pos, m_currZoom);
  if(tileId == prevTileId) {
    if(auto tex = prevTex.lock())
      return elevationLerp(*tex.get(), tileId, pos);
  }

  int minz = std::max(0, tileId.z - 6);  // MAX_LOD
  do {
    auto newtex = m_elevationSource->getTexture(tileId);
    if(newtex) {
      prevTileId = tileId;
      prevTex = newtex;
      return elevationLerp(*newtex, tileId, pos);
    }
    tileId = tileId.getParent();
  } while(/*ascend &&*/ tileId.z >= minz);
  ok = false;
  return 0;
}

bool ElevationManager::hasTile(TileID tileId)
{
  bool ok = false;
  setZoom(tileId.z);
  getElevation(MapProjection::tileCenter(tileId), ok);
  return ok;
  //return bool(m_elevationSource->getTexture(tileId));
}

//static std::atomic<int> nqueued {0};

void ElevationManager::renderTerrainDepth(RenderState& _rs, const View& _view,
                                          const std::vector<std::shared_ptr<Tile>>& _tiles)
{
  FrameInfo::scope _trace("renderTerrainDepth");

  /*
  int w = _view.getWidth()/bufferScale, h = _view.getHeight()/bufferScale;
  if (!m_frameBuffer || m_frameBuffer->getWidth() != w || m_frameBuffer->getHeight() != h) {
    m_frameBuffer = std::make_unique<FrameBuffer>(w, h, false, GL_R32UI);
    m_depthData.resize(w * h, 1.0f);

    _rs.m_terrainDepthTexture = m_frameBuffer->getTextureHandle();
  }

  _rs.cacheDefaultFramebuffer();
  m_frameBuffer->applyAsRenderTarget(_rs);
  m_style->draw(_rs, _view, _tiles, {});

  //GL::readPixels(0, 0, w, h, GL_RED_INTEGER, GL_UNSIGNED_INT, m_depthData.data());
  _rs.framebuffer(_rs.defaultFrameBuffer());
  */

  if(!offscreenWorker) {
    LOGE("Offscreen worker has not been created!");
    return;
  }

  if(!m_renderState)
    m_renderState = std::make_unique<RenderState>();

  std::mutex drawMutex;
  std::condition_variable drawCond;
  bool drawFinished = false;
  std::unique_lock<std::mutex> mainLock(drawMutex); //, std::defer_lock);

  offscreenWorker->enqueue([&](){
    std::unique_lock<std::mutex> workerLock(drawMutex);
    m_renderState->flushResourceDeletion();
    int w = _view.getWidth()/bufferScale, h = _view.getHeight()/bufferScale;
    if (!m_frameBuffer || m_frameBuffer->getWidth() != w || m_frameBuffer->getHeight() != h) {
      m_frameBuffer = std::make_unique<FrameBuffer>(w, h, false, GL_R32UI);
      m_depthData.resize(w * h, 1.0f);
    }
    m_frameBuffer->applyAsRenderTarget(*m_renderState);  // this does the glClear()

    // originally, we were reusing mesh from another style, but this will use the uniform location for the
    //  other style (since SharedMesh saves Style*); also creates problems when deleting Scene if
    //  first raster tile was drawn by offscreen worker; also, VAOs can't be shared between contexts
    m_style->draw(*m_renderState, _view, _tiles, {});

    drawFinished = true;
    workerLock.unlock();
    drawCond.notify_all();

    GL::readPixels(0, 0, w, h, GL_RED_INTEGER, GL_UNSIGNED_INT, m_depthData.data());
  });

  // wait for draw to finish to avoid, e.g., duplicate texture uploads
  drawCond.wait(mainLock, [&]{ return drawFinished; });
}

float ElevationManager::getDepth(glm::vec2 screenpos)
{
  if(!m_frameBuffer || m_depthData.empty()) { return 0; }
  // for now, clamp to screen bounds to handle offscreen labels (extendedBounds in processLabelUpdate())
  int w = m_frameBuffer->getWidth(), h = m_frameBuffer->getHeight();
  glm::vec2 pos = glm::clamp(glm::round(screenpos/bufferScale), {0, 0}, {w-1, h-1});
  //GL::readPixels(floorf(screenpos.x), floorf(screenpos.y), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &pixel);
  // convert from 0..1 (glDepthRange) to -1..1 (NDC)
  //return 2*m_depthData[int(pos.x) + int(h - pos.y - 1)*w] - 1;
  return 1/m_depthData[int(pos.x) + int(h - pos.y - 1)*w];
}

void ElevationManager::setZoom(int z)
{
  m_currZoom = std::min(m_elevationSource->maxZoom(), z);
}

ElevationManager::ElevationManager(std::shared_ptr<RasterSource> src, Style& style) : m_elevationSource(src)
{
  //m_elevationSource->m_keepTextureData = true;  -- now done in Scene::load()

  // default blending mode is opaque, as desired
  m_style = std::make_unique<TerrainStyle>("__terrain");
  m_style->getShaderSource() = style.getShaderSource();
  // direct assignment doesn't work (operator= deleted on std:pair!?)
  for(auto& uniform : style.styleUniforms()) {
    m_style->styleUniforms().emplace_back(uniform.first.name, uniform.second);
  }
  m_style->setID(style.getID());  // use same mesh
  m_style->setRasterType(RasterType::custom);
}

ElevationManager::~ElevationManager()
{
  offscreenWorker->enqueue([_style=m_style.release(), _fb=m_frameBuffer.release()](){
    delete _style;
    delete _fb;
    //delete _rs;
  });
}

} // namespace Tangram
