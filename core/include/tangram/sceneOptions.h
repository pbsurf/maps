#pragma once

#include "util/url.h"

#include <functional>
#include <string>
#include <vector>


namespace Tangram {

class Scene;

struct SceneUpdate {
    std::string path;
    std::string value;
    SceneUpdate(std::string p, std::string v) : path(p), value(v) {}
    SceneUpdate() {}
};


class SceneOptions {
public:
    explicit SceneOptions(const Url& _url, bool _useScenePosition = false,
                          const std::vector<SceneUpdate>& _updates = {})
        : url(_url), updates(_updates),
          useScenePosition(_useScenePosition) {}

    explicit SceneOptions(const std::string& _yaml, const Url& _resources,
                          bool _useScenePosition = false,
                          const std::vector<SceneUpdate>& _updates = {})
        : yaml(_yaml), url(_resources), updates(_updates),
          useScenePosition(_useScenePosition) {}

    SceneOptions() : numTileWorkers(0) {}

    /// Scene as yaml string
    std::string yaml;

    /// URL from which this scene is loaded, or resourceRoot
    Url url;

    /// SceneUpdates to apply to the scene
    std::vector<SceneUpdate> updates;

    /// Set the view to the position provided by the scene
    bool useScenePosition = true;

    /// Add styles toggled by DebugFlags
    bool debugStyles = false;

    /// Start loading tiles as soon as possible
    bool prefetchTiles = true;

    /// Number of threads fetching tiles
    uint32_t numTileWorkers = 2;

    /// 16MB default in-memory DataSource cache
    size_t memoryTileCacheSize = CACHE_SIZE;

    /// persistent MBTiles DataSource cache
    size_t diskTileCacheSize = 0;

    std::string diskTileCacheDir;

private:
    static constexpr size_t CACHE_SIZE = 16 * (1024 * 1024);

};

}
