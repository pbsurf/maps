#pragma once

#include "data/tileSource.h"

namespace Tangram {

class DataSourceContext;  //class Platform;

class NetworkDataSource : public TileSource::DataSource {
public:

    NetworkDataSource(DataSourceContext& _context, std::string url, UrlOptions options);

    bool loadTileData(std::shared_ptr<TileTask> _task, TileTaskCb _cb) override;

    void cancelLoadingTile(TileTask& _task) override;

    static std::string tileCoordinatesToQuadKey(const TileID& tile);

    /// Returns true if the URL either contains 'x', 'y', and 'z' placeholders or contains a 'q' placeholder.
    static bool urlHasTilePattern(const std::string& url);

    static std::string buildUrlForTile(const TileID& tile, const std::string& urlTemplate, const UrlOptions& options, int subdomainIndex);

private:

    DataSourceContext& m_context;  //Platform& m_platform;

    std::string m_urlTemplate;

    int m_urlFunction = -1;

    UrlOptions m_options;

    int m_urlSubdomainIndex = 0;
};

}
