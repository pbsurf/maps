#include "data/rasterSource.h"
#include "data/propertyItem.h"
#include "data/tileData.h"
#include "tile/tile.h"
#include "tile/tileBuilder.h"
#include "tile/tileTask.h"
#include "util/mapProjection.h"
#include "log.h"

namespace Tangram {

class RasterTileTask : public BinaryTileTask {
public:

    const bool subTask = false;

    std::unique_ptr<Texture> texture;
    std::unique_ptr<Raster> raster;

    RasterTileTask(const TileID& _tileId, std::shared_ptr<TileSource> _source, bool _subTask)
        : BinaryTileTask(_tileId, _source),
          subTask(_subTask) {}


    std::shared_ptr<RasterSource> rasterSource() {
        return reinterpret_cast<std::weak_ptr<RasterSource>*>(&m_source)->lock();
    }

    bool hasData() const override {
        // probably should be "return BinaryTileTask::hasData() || ..."
        return bool(rawTileData) || bool(texture) || bool(raster);
    }

    bool isReady() const override {
        if (!subTask) {
            return bool(m_tile);
        } else {
            return bool(texture) || bool(raster);
        }
    }

    void process(TileBuilder& _tileBuilder) override {
        auto source = rasterSource();
        if (!source) { return; }

        if (!texture && !raster) {
            // Decode texture data
            texture = source->createTexture(m_tileId, *rawTileData);
            if (!texture) {
                raster = std::make_unique<Raster>(m_tileId, source->emptyTexture());
            }
        }

        // Create tile geometries
        if (!subTask) {
            if(1)  //source->name() == "elevation")
              m_tile = _tileBuilder.build(m_tileId, *(source->m_gridData), *source);
            else
              m_tile = _tileBuilder.build(m_tileId, *(source->m_tileData), *source);
            m_ready = true;
        }
    }

    void addRaster(Tile& _tile) {
        auto source = rasterSource();
        if (!source) { return; }

        auto& rasters = _tile.rasters();

        if (raster) {
            rasters.emplace_back(raster->tileID, raster->texture);
        } else {
            auto tex = source->cacheTexture(m_tileId, std::move(texture));
            rasters.emplace_back(m_tileId, tex);
        }
    }

    void complete() override {

        addRaster(*m_tile);

        for (auto& subTask : m_subTasks) {
            //assert(subTask->isReady());
            subTask->complete(*this);
        }
    }

    void complete(TileTask& _mainTask) override {
        if (!isReady()) {  //isCanceled()?
            _mainTask.tile()->rasters().emplace_back(tileId(), rasterSource()->m_emptyTexture);
        } else {
            addRaster(*_mainTask.tile());
        }
    }
};


RasterSource::RasterSource(const std::string& _name, std::unique_ptr<DataSource> _sources,
                           TextureOptions _options, TileSource::ZoomOptions _zoomOptions)
    : TileSource(_name, std::move(_sources), _zoomOptions),
      m_texOptions(_options) {

    m_textures = std::make_shared<Cache>();
    m_emptyTexture = std::make_shared<Texture>(m_texOptions);

    GLubyte pixel[4] = { 0, 0, 0, 0 };
    auto bpp = _options.bytesPerPixel();
    m_emptyTexture->setPixelData(1, 1, bpp, pixel, bpp);
}

void RasterSource::generateGeometry(bool _generateGeometry) {
    m_generateGeometry = _generateGeometry;

    if (m_generateGeometry) {
        Feature rasterFeature;
        rasterFeature.geometryType = GeometryType::polygons;
        rasterFeature.polygons = { { {
                    {0.0f, 0.0f},
                    {1.0f, 0.0f},
                    {1.0f, 1.0f},
                    {0.0f, 1.0f},
                    {0.0f, 0.0f}
                } } };
        rasterFeature.props = Properties();

        m_tileData = std::make_shared<TileData>();
        m_tileData->layers.emplace_back("");
        m_tileData->layers.back().features.push_back(rasterFeature);

        // grid for 3D terrain
        uint32_t resolution = 64;
        float w = 1.f / resolution;
        Feature gridFeature;
        gridFeature.geometryType = GeometryType::polygons;
        gridFeature.props = Properties();
        for (uint32_t col = 0; col < resolution; col++) {
          for (uint32_t row = 0; row < resolution; row++) {
            float x0 = w*row, y0 = w*col;
            gridFeature.polygons.push_back({ {
                {x0,   y0},
                {x0+w, y0},
                {x0+w, y0+w},
                {x0,   y0+w},
                {x0,   y0} } });
          }
        }

        m_gridData = std::make_shared<TileData>();
        m_gridData->layers.emplace_back("");
        m_gridData->layers.back().features.push_back(gridFeature);
    }
}

std::unique_ptr<Texture> RasterSource::createTexture(TileID _tile, const std::vector<char>& _rawTileData) {
    if (_rawTileData.empty()) { return nullptr; }

    auto data = reinterpret_cast<const uint8_t*>(_rawTileData.data());
    auto length = _rawTileData.size();

    return std::make_unique<Texture>(data, length, m_texOptions);
}

std::shared_ptr<TileData> RasterSource::parse(const TileTask& _task) const {
    assert(false);
    return nullptr;
}

void RasterSource::addRasterTask(TileTask& _task) {

    TileID subTileID = _task.tileId();

    // apply apt downsampling for raster tiles depending on difference
    // in zoomBias (which also takes zoom offset into account)
    auto zoomDiff = m_zoomOptions.zoomBias - _task.source()->zoomBias();

    if (zoomDiff > 0) {
        subTileID = subTileID.zoomBiasAdjusted(zoomDiff).withMaxSourceZoom(m_zoomOptions.maxZoom);
    } else {
        subTileID = subTileID.withMaxSourceZoom(m_zoomOptions.maxZoom);
    }

    auto rasterTask = createRasterTask(subTileID, true);

    _task.subTasks().push_back(rasterTask);
}

std::shared_ptr<RasterTileTask> RasterSource::createRasterTask(TileID _tileId, bool subTask) {
    auto task = std::make_shared<RasterTileTask>(_tileId, shared_from_this(), subTask);

    // First try existing textures cache
    TileID id(_tileId.x, _tileId.y, _tileId.z);

    auto texIt = m_textures->find(id);
    if (texIt != m_textures->end()) {
        auto texture = texIt->second.lock();

        if (texture) {
            LOGV("%d - reuse %s", m_textures->size(), id.toString().c_str());

            task->raster = std::make_unique<Raster>(id, texture);
            // No more loading needed.
            task->startedLoading();
        }
    }
    return task;
}

std::shared_ptr<TileTask> RasterSource::createTask(TileID _tileId) {
    auto task = createRasterTask(_tileId, false);

    addRasterTasks(*task);

    return task;
}

std::shared_ptr<Texture> RasterSource::cacheTexture(const TileID& _tileId, std::unique_ptr<Texture> _texture) {
    TileID id(_tileId.x, _tileId.y, _tileId.z);

    auto& textureEntry = (*m_textures)[id];
    auto texture = textureEntry.lock();
    if (texture) {
        LOGV("%d - drop duplicate %s", m_textures->size(), id.toString().c_str());
        // The same texture has been loaded in the meantime: Reuse it and drop _texture..
        return texture;
    }

    texture = std::shared_ptr<Texture>(_texture.release(),
                                       [c = std::weak_ptr<Cache>(m_textures), id](auto* t) {
                                           if (auto cache = c.lock()) {
                                               cache->erase(id);
                                               LOGV("%d - remove %s", cache->size(), id.toString().c_str());
                                           }
                                           delete t;
                                       });
    // Add to cache
    textureEntry = texture;
    LOGV("%d - added %s", m_textures->size(), id.toString().c_str());

    return texture;
}

}
