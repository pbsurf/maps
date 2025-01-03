#pragma once

#include "gl/texture.h"
#include "tile/tileID.h"
#include "util/fastmap.h"
#include "util/types.h"

#include "glm/mat4x4.hpp"
#include "glm/vec2.hpp"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Tangram {

class MapProjection;
struct Properties;
class Style;
class View;
struct StyledMesh;

struct Raster {
    TileID tileID;
    std::shared_ptr<Texture> texture;

    Raster(TileID tileID, std::shared_ptr<Texture> texture) : tileID(tileID), texture(texture) {}
    Raster(Raster&& other) : tileID(other.tileID), texture(std::move(other.texture)) {}

    bool isValid() const { return texture != nullptr; }
};

/* Tile of vector map data
 *
 * Tile represents a fixed area of a map at a fixed zoom level; It contains its
 * position within a quadtree of tiles and its location in projected global
 * space; It stores drawable geometry of the map features in its area
 */
class Tile {

public:

    Tile(TileID _id, const int32_t& _sourceId = 0, const int32_t& _sourceGeneration = 0);

    virtual ~Tile();

    /* Returns the immutable <TileID> of this tile */
    const TileID& getID() const { return m_id; }

    /* Returns the center of the tile area in projection units */
    const glm::dvec2& getOrigin() const { return m_tileOrigin; }

    /* Returns the length of a side of this tile in projection units */
    double getScale() const { return m_scale; }

    double getInverseScale() const { return 1.0 / m_scale; }

    const glm::mat4& getModelMatrix() const { return m_modelMatrix; }

    const glm::mat4& mvp() const { return m_mvp; }

    LngLat coordToLngLat(const glm::vec2& _tileCoord) const;

    void initGeometry(uint32_t _size);

    const std::unique_ptr<StyledMesh>& getMesh(const Style& _style) const;

    void setMesh(const Style& _style, std::unique_ptr<StyledMesh> _mesh);

    void setSelectionFeatures(const fastmap<uint32_t, std::shared_ptr<Properties>> _selectionFeatures);

    std::shared_ptr<Properties> getSelectionFeature(uint32_t _id) const;

    const auto& getSelectionFeatures() const { return m_selectionFeatures; }

    auto& rasters() { return m_rasters; }
    const auto& rasters() const { return m_rasters; }

    /* Update the Tile considering the current view */
    void update(const View& _view, float _dt);

    void resetState();

    /* Get the sum in bytes of static <Mesh>es */
    size_t getMemoryUsage() const;

    int64_t sourceGeneration() const { return m_sourceGeneration; }

    int32_t sourceID() const { return m_sourceId; }

    int8_t proxyDepth() const { return m_proxyDepth; }
    bool isProxy() const { return m_proxyDepth > 0; }

    void setProxyDepth(int8_t _depth) { m_proxyDepth = _depth; }

private:

    const TileID m_id;

    double m_scale = 1;

    /* ID of the TileSource */
    const int32_t m_sourceId;

    /* State of the TileSource for which this tile was created */
    const int64_t m_sourceGeneration;

    int8_t m_proxyDepth = 0;

    glm::dvec2 m_tileOrigin; // South-West corner of the tile in 2D projection space in meters (e.g. mercator meters)

    glm::mat4 m_modelMatrix; // Matrix relating tile-local coordinates to global projection space coordinates;
    // Note that this matrix does not contain the relative translation from the global origin to the tile origin.
    // Distances from the global origin are too large to represent precisely in 32-bit floats, so we only apply the
    // relative translation from the view origin to the model origin immediately before drawing the tile.

    glm::mat4 m_mvp;

    // Map of <Style>s and their associated <Mesh>es
    std::vector<std::unique_ptr<StyledMesh>> m_geometry;
    std::vector<Raster> m_rasters;

    mutable size_t m_memoryUsage = 0;

    fastmap<uint32_t, std::shared_ptr<Properties>> m_selectionFeatures;

};

}
