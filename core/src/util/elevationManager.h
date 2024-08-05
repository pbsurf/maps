#pragma once

#include <memory>
#include "util/mapProjection.h"

namespace Tangram {

class RasterSource;

class ElevationManager
{
public:
  ElevationManager(std::shared_ptr<RasterSource> src);
  double getElevation(ProjectedMeters pos, bool& ok);
  float getDepth(glm::vec2 screenpos);
  void setZoom(int z) { m_currZoom = z; }

  std::shared_ptr<RasterSource> m_elevationSource;
  int m_currZoom = 0;
};

}
