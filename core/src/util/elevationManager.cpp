#include "util/elevationManager.h"
#include "data/rasterSource.h"
//#include "gl/texture.h"

namespace Tangram {

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
  using namespace Tangram;

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

static TileID lngLatTile(LngLat ll, int z)
{
  int x = int(floor((ll.longitude + 180.0) / 360.0 * (1 << z)));
  double latrad = ll.latitude * M_PI/180.0;
  int y = int(floor((1.0 - asinh(tan(latrad)) / M_PI) / 2.0 * (1 << z)));
  return TileID(x, y, z);
}

static TileID projMetersTile(ProjectedMeters ll, int z)
{
  constexpr double hc = MapProjection::EARTH_HALF_CIRCUMFERENCE_METERS;
  double metersPerTile = MapProjection::metersPerTileAtZoom(z);
  return TileID(int((ll.x + hc)/metersPerTile), int((hc - ll.y)/metersPerTile), z);
}

double ElevationManager::getElevation(ProjectedMeters pos, bool& ok)
{
  static std::weak_ptr<Texture> prevTex;
  static TileID prevTileId = {0, 0, 0, 0};

  ok = true;
  //TileID llId = lngLatTile(MapProjection::projectedMetersToLngLat(pos), m_currZoom);
  TileID tileId = projMetersTile(pos, m_currZoom);
  auto tex = prevTex.lock();
  if(tex && tileId == prevTileId)
    return elevationLerp(*tex.get(), tileId, pos);

  auto newtex = m_elevationSource->getTexture(tileId);
  if(newtex) {
    prevTileId = tileId;
    prevTex = newtex;
    return elevationLerp(*newtex, tileId, pos);
  }
  ok = false;
  return 0;
}

ElevationManager::ElevationManager(std::shared_ptr<RasterSource> src) : m_elevationSource(src)
{
  m_elevationSource->m_keepTextureData = true;
}

} // namespace Tangram
