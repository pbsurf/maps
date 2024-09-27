#include "util/elevationManager.h"
#include "data/rasterSource.h"
#include "style/polygonStyle.h"
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
  depthOut = floatBitsToUint(gl_FragCoord.z);
}
)RAW_GLSL";

#include "polygon_vs.h"

#include "rasters_glsl.h"
#include "scene/scene.h"
#include "gl/shaderProgram.h"

#include "../../platforms/common/platform_gl.h"

namespace Tangram {

class TerrainStyle : public PolygonStyle
{
public:
  using PolygonStyle::PolygonStyle;
  //void constructShaderProgram() override {
  //  PolygonStyle::constructShaderProgram();
  //  m_shaderSource->setSourceStrings(terrain_depth_fs, polygon_vs);
  //}

  void build(const Scene& _scene) override {

      constructVertexLayout();
      m_shaderSource->setSourceStrings(terrain_depth_fs, polygon_vs);

      m_shaderSource->addSourceBlock("defines", "#define TANGRAM_TERRAIN_3D\n", false);

      if (m_rasterType != RasterType::none) {
          int numRasterSource = 0;
          for (const auto& source : _scene.tileSources()) {
              if (source->isRaster()) { numRasterSource++; }
          }
          if (numRasterSource > 0) {
              m_shaderSource->addSourceBlock("defines", "#define TANGRAM_NUM_RASTER_SOURCES "
                                             + std::to_string(numRasterSource) + "\n", false);
              m_shaderSource->addSourceBlock("defines", "#define TANGRAM_MODEL_POSITION_BASE_ZOOM_VARYING\n", false);

              m_shaderSource->addSourceBlock("raster", rasters_glsl);
          }
      }

      std::string vertSrc = m_shaderSource->buildVertexSource();
      std::string fragSrc = terrain_depth_fs;  //m_shaderSource->buildFragmentSource();

      m_shaderProgram = std::make_shared<ShaderProgram>(vertSrc, fragSrc, m_vertexLayout.get());
      m_shaderProgram->setDescription("{style:" + m_name + "}");

      // Clear ShaderSource builder
      m_shaderSource.reset();
  }
};

static double readElevTex(const Texture& tex, int x, int y)
{
  // see getElevation() in raster-contour.yaml and https://github.com/tilezen/joerd
  if(tex.getOptions().pixelFormat == PixelFormat::FLOAT)
    return ((float*)tex.bufferData())[y*tex.width() + x];
  GLubyte* p = tex.bufferData() + y*tex.width()*4 + x*4;
  //(red * 256 + green + blue / 256) - 32768
  return (p[0]*256 + p[1] + p[2]/256.0) - 32768;
}

static double elevationLerp(const Texture& tex, TileID tileId, ProjectedMeters meters)
{
  double scale = MapProjection::metersPerTileAtZoom(tileId.z);
  ProjectedMeters tileOrigin = MapProjection::tileSouthWestCorner(tileId);
  //ProjectedMeters meters = MapProjection::lngLatToProjectedMeters(pos);  //glm::dvec2(tileCoord) * scale + tileOrigin;
  ProjectedMeters offset = meters - tileOrigin;
  double ox = offset.x/scale, oy = offset.y/scale;
  if(ox < 0 || ox > 1 || oy < 0 || oy > 1)
    return 0;  //LOGE("Elevation tile position out of range");
  // ... seems this works correctly w/o accounting for vertical flip of texture
  double x0 = ox*tex.width() - 0.5, y0 = oy*tex.height() - 0.5;  // -0.5 to adjust for pixel centers
  // we should extrapolate at edges instead of clamping - see shader in raster_contour.yaml
  int ix0 = std::max(0, int(std::floor(x0))), iy0 = std::max(0, int(std::floor(y0)));
  int ix1 = std::min(int(std::ceil(x0)), tex.width()-1), iy1 = std::min(int(std::ceil(y0)), tex.height()-1);
  double fx = x0 - ix0, fy = y0 - iy0;
  double t00 = readElevTex(tex, ix0, iy0);
  double t01 = readElevTex(tex, ix0, iy1);
  double t10 = readElevTex(tex, ix1, iy0);
  double t11 = readElevTex(tex, ix1, iy1);
  double t0 = t00 + fx*(t10 - t00);
  double t1 = t01 + fx*(t11 - t01);
  return t0 + fy*(t1 - t0);
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

void ElevationManager::renderTerrainDepth(RenderState& _rs, const View& _view,
                                          const std::vector<std::shared_ptr<Tile>>& _tiles)
{
  //static const GLenum drawbuffs[] = {GL_NONE, GL_COLOR_ATTACHMENT0};

  static GLsync sync = 0;

  static GLuint pbo[2] = {0, 0};

  int w = _view.getWidth(), h = _view.getHeight();

  size_t nbytes = w*h*4;

  if (!m_frameBuffer || m_frameBuffer->getWidth() != w || m_frameBuffer->getHeight() != h) {
    m_frameBuffer = std::make_unique<FrameBuffer>(w, h, false, GL_R32UI);
    m_depthData.resize(w * h, 1.0f);

    // setup PBO
    if(pbo[0] > 0) { GL::deleteBuffers(2, pbo); }

    GL::genBuffers(2, pbo);
    GL::bindBuffer(GL_PIXEL_PACK_BUFFER, pbo[0]);
    GL::bufferData(GL_PIXEL_PACK_BUFFER, nbytes, NULL, GL_STREAM_READ);  // NULL instructs GL to allocate buffer
    GL::bindBuffer(GL_PIXEL_PACK_BUFFER, pbo[1]);
    GL::bufferData(GL_PIXEL_PACK_BUFFER, nbytes, NULL, GL_STREAM_READ);  // NULL instructs GL to allocate buffer
    GL::bindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  //} else {
  //  GL::bindBuffer(GL_PIXEL_PACK_BUFFER, pbo[0]);
  //  GLubyte* ptr = (GLubyte*)GL::mapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
  //  //~GLubyte* ptr = (GLubyte*)GL::mapBufferRange(GL_PIXEL_PACK_BUFFER, 0, nbytes, GL_MAP_READ_BIT)
  //  if(ptr) { /* DO STUFF */
  //    memcpy(m_depthData.data(), ptr, nbytes);
  //  }
  //  GL::unmapBuffer(GL_PIXEL_PACK_BUFFER);
  //  GL::bindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  //  std::swap(pbo[0], pbo[1]);
  }

  //GL::finish();

  if(sync != 0) {
    GLenum status = glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
    LOG("Status later: %x", status);
    if(status == GL_TIMEOUT_EXPIRED) {
      return;
    }
    glDeleteSync(sync);
  }

  FrameInfo::begin("renderTerrainDepth");

  _rs.cacheDefaultFramebuffer();
  m_frameBuffer->applyAsRenderTarget(_rs);
  // VAO?
  //GL::drawBuffers(2, &drawbuffs[0]);
  m_style->draw(_rs, _view, _tiles, {});
  //GL::drawBuffers(1, &drawbuffs[1]);

  GLsync sync2 = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  GLenum status2 = glClientWaitSync(sync2, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
  LOG("Status after draw: %x", status2	);
  glDeleteSync(sync2);

  // TODO: use PBO to make this async
  // refs: songho.ca/opengl/gl_pbo.html ; roxlu.com/2014/048/fast-pixel-transfers-with-pixel-buffer-objects

  // read pixels
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  GL::bindBuffer(GL_PIXEL_PACK_BUFFER, pbo[0]);
  GL::readPixels(0, 0, w, h, GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);  // NULL to read into PBO
  //GL::readPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, NULL);  // NULL to read into PBO

  sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

  GLenum status = glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
  LOG("Status: %x", status);

  GL::bindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  //~// do some other stuff, ... then access pixels

  std::swap(pbo[0], pbo[1]);
  /*
  GL::bindBuffer(GL_PIXEL_PACK_BUFFER, pbo[0]);

  //GLubyte* ptr = (GLubyte*)GL::mapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
  GLubyte* ptr = (GLubyte*)GL::mapBufferRange(GL_PIXEL_PACK_BUFFER, 0, nbytes, GL_MAP_READ_BIT);
  if(ptr) {
    memcpy(m_depthData.data(), ptr, nbytes);
  }
  GL::unmapBuffer(GL_PIXEL_PACK_BUFFER);
  GL::bindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  */

  //GL::readPixels(0, 0, w, h, GL_RED_INTEGER, GL_UNSIGNED_INT, m_depthData.data());
  _rs.framebuffer(_rs.defaultFrameBuffer());

  FrameInfo::end("renderTerrainDepth");
}

float ElevationManager::getDepth(glm::vec2 screenpos)
{
  // for now, clamp to screen bounds to handle offscreen labels (extendedBounds in processLabelUpdate())
  int w = m_frameBuffer->getWidth(), h = m_frameBuffer->getHeight();
  glm::vec2 pos = glm::clamp(screenpos, {0, 0}, {w-1, h-1});
  //GL::readPixels(floorf(screenpos.x), floorf(screenpos.y), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &pixel);
  // convert from 0..1 (glDepthRange) to -1..1 (NDC)
  return 2*m_depthData[floorf(pos.x) + floorf(h - pos.y - 1)*w] - 1;
}

void ElevationManager::setZoom(int z)
{
  m_currZoom = std::min(m_elevationSource->maxZoom(), z);
}

ElevationManager::ElevationManager(std::shared_ptr<RasterSource> src, Style& style) : m_elevationSource(src)
{
  m_elevationSource->m_keepTextureData = true;

  // default blending mode is opaque, as desired
  m_style = std::make_unique<TerrainStyle>("__terrain");
  m_style->getShaderSource() = style.getShaderSource();
  // direct assignment doesn't work (operator= deleted on std:pair!?)
  for(auto& uniform : style.styleUniforms()) {
    m_style->styleUniforms().emplace_back(uniform.first.name, uniform.second);
  }
  m_style->setID(style.getID());  // use same mesh
  m_style->setRasterType(RasterType::custom);
  //m_style->getShaderSource().setSourceStrings(terrain_depth_fs, polygon_vs);
}

} // namespace Tangram
