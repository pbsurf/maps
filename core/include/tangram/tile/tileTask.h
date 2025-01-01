#pragma once

#include "tile/tileID.h"
#include "platform.h" // UrlRequestHandle

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace Tangram {

class TileManager;
class TileBuilder;
class TileSource;
class Tile;
class MapProjection;
class ScenePrana;
struct TileData;


class TileTask {

public:

    TileTask(const TileID& _tileId, TileSource* _source);

    // No copies
    TileTask(const TileTask& _other) = delete;
    TileTask& operator=(const TileTask& _other) = delete;

    virtual ~TileTask();

    virtual bool hasData() const { return true; }

    virtual bool isReady() const { return !needsLoading() && bool(m_ready); }
    void setReady() { m_ready = true; }

    Tile* tile() { return m_tile.get(); }

    std::unique_ptr<Tile> getTile();
    void setTile(std::unique_ptr<Tile>&& _tile);

    TileSource* source() { return m_source; }
    std::shared_ptr<ScenePrana> prana() { return m_scenePrana.lock(); }
    void setScenePrana(std::weak_ptr<ScenePrana> _prana);
    int64_t sourceId() { return m_sourceId; }
    int64_t sourceGeneration() const { return m_sourceGeneration; }

    TileID tileId() const { return m_tileId; }

    void cancel() { m_canceled = true; }
    bool isCanceled() const { return m_canceled; }

    double getPriority() const {
        return m_priority.load();
    }

    void setPriority(double _priority) {
        m_priority.store(_priority);
    }

    void setProxyState(bool isProxy) { m_proxyState = isProxy; }
    bool isProxy() const { return m_proxyState; }

    auto& subTasks() { return m_subTasks; }

    // running on worker thread
    virtual void process(TileBuilder& _tileBuilder);

    // running on main thread when the tile is added to
    virtual void complete();

    // onDone for sub-tasks
    virtual void complete(TileTask& _mainTask) {}

    bool needsLoading() const { return m_needsLoading; }

    // Set whether DataSource should (re)try loading data
    void setNeedsLoading(bool _needsLoading) {
         m_needsLoading = _needsLoading;
    }

    void startedLoading() { m_needsLoading = false; }

    int rawSource = 0;
    int offlineId = 0;
    int shareCount = 0;

protected:

    const TileID m_tileId;

    TileSource* m_source;

    // locking this prevents Scene destruction (so m_source is valid)
    std::weak_ptr<ScenePrana> m_scenePrana;

    // Vector of tasks to download raster samplers
    std::vector<std::shared_ptr<TileTask>> m_subTasks;

    const int64_t m_sourceId;
    const int64_t m_sourceGeneration;

    // Tile result, set when tile is successfully created
    std::unique_ptr<Tile> m_tile;

    std::atomic<bool> m_ready;
    std::atomic<bool> m_canceled;
    std::atomic<bool> m_needsLoading;

    std::atomic<float> m_priority;
    std::atomic<bool> m_proxyState;
};

class BinaryTileTask : public TileTask {
public:
    BinaryTileTask(const TileID& _tileId, TileSource* _source)
        : TileTask(_tileId, _source) {}

    virtual bool hasData() const override {
        return rawTileData && !rawTileData->empty();
    }
    // Raw tile data that will be processed by TileSource.
    std::shared_ptr<std::vector<char>> rawTileData;

    bool dataFromCache = false;
    UrlRequestHandle urlRequestHandle = 0;
};

struct TileTaskQueue {
    virtual void enqueue(std::shared_ptr<TileTask> task) = 0;
};

struct TileTaskCb {
    std::function<void(std::shared_ptr<TileTask>)> func;
};

}
