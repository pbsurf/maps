#pragma once

#include <memory>
#include <mutex>
#include "util/mapProjection.h"

namespace Tangram {

class RasterSource;
class Style;
class FrameBuffer;
class RenderState;
class View;
class Tile;
class AsyncWorker;

class ElevationManager
{
public:
  ElevationManager(std::shared_ptr<RasterSource> src, Style& style);
  double getElevation(ProjectedMeters pos, bool& ok, bool ascend = false);
  float getDepth(glm::vec2 screenpos);
  void setZoom(int z);
  bool hasTile(TileID tileId);

  void renderTerrainDepth(RenderState& _rs, const View& _view,
                          const std::vector<std::shared_ptr<Tile>>& _tiles);

  std::shared_ptr<RasterSource> m_elevationSource;
  int m_currZoom = 0;

  std::unique_ptr<Style> m_style;
  std::vector<float> m_depthData;
  std::unique_ptr<FrameBuffer> m_frameBuffer;
  std::unique_ptr<RenderState> m_renderState;

  static std::unique_ptr<AsyncWorker> offscreenWorker;
};

}
