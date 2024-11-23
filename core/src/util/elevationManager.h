#pragma once

#include <memory>
#include <mutex>
#include <atomic>
#include "util/mapProjection.h"

namespace Tangram {

class RasterSource;
class Style;
class FrameBuffer;
class RenderState;
class View;
class Tile;
class AsyncWorker;
class Texture;

class ElevationManager
{
public:
  ElevationManager(std::shared_ptr<RasterSource> src, Style& style);
  ~ElevationManager();
  double getElevation(ProjectedMeters pos, bool& ok);
  float getDepth(glm::vec2 screenpos);
  bool hasTile(TileID tileId);
  void setMinZoom(int z) { m_minZoom = z; }

  void renderTerrainDepth(RenderState& _rs, const View& _view,
                          const std::vector<std::shared_ptr<Tile>>& _tiles);

  static double elevationLerp(const Texture& tex, glm::vec2 pos, glm::vec2* gradOut = nullptr);
  static double elevationLerp(const Texture& tex, TileID tileId, ProjectedMeters meters);

  void drawDepthDebug(RenderState& _rs, glm::vec2 _dim);

  std::shared_ptr<RasterSource> m_elevationSource;

  std::unique_ptr<Style> m_style;
  std::vector<float> m_depthData;
  std::unique_ptr<FrameBuffer> m_frameBuffer;
  std::atomic<float> m_depthBaseZoom{0.0f};
  int m_minZoom = 0;

  static std::unique_ptr<RenderState> m_renderState;
  static std::unique_ptr<AsyncWorker> offscreenWorker;
};

}
