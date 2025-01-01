#include "tile/tileTask.h"

#include "data/tileSource.h"
#include "scene/scene.h"
#include "tile/tile.h"
#include "tile/tileBuilder.h"
#include "util/mapProjection.h"

namespace Tangram {

TileTask::TileTask(const TileID& _tileId, TileSource* _source) :
    m_tileId(_tileId),
    m_source(_source),
    m_sourceId(_source ? _source->id() : 0),
    m_sourceGeneration(_source ? _source->generation() : 0),
    m_ready(false),
    m_canceled(false),
    m_needsLoading(true),
    m_priority(0),
    m_proxyState(false) {}

TileTask::~TileTask() {}

std::unique_ptr<Tile> TileTask::getTile() {
    return std::move(m_tile);
}

void TileTask::setTile(std::unique_ptr<Tile>&& _tile) {
    m_tile = std::move(_tile);
    m_ready = true;
}

void TileTask::process(TileBuilder& _tileBuilder) {

    auto tileData = m_source->parse(*this);

    if (tileData) {
        m_tile = std::make_unique<Tile>(m_tileId, m_source->id(), m_source->generation());
        _tileBuilder.build(*m_tile, *tileData, *m_source);
        m_ready = true;
    } else {
        cancel();
    }
}

void TileTask::complete() {

    for (auto& subTask : m_subTasks) {
        //assert(subTask->isReady());
        subTask->complete(*this);
    }

}

// Getting ScenePrana into each TileSource so that it can be set when TileTask is created would be messier
void TileTask::setScenePrana(std::weak_ptr<ScenePrana> _prana) {
    m_scenePrana = _prana;
    for (auto& subTask : m_subTasks) {
        subTask->setScenePrana(_prana);
    }
}

}
