#include "data/networkDataSource.h"

#include "log.h"
#include "platform.h"
#include "util/mapProjection.h"
#include "util/util.h"
#include "scene/scene.h"
#include "js/JavaScript.h"

namespace Tangram {

NetworkDataSource::NetworkDataSource(DataSourceContext& _context, std::string url, UrlOptions options) :
    m_context(_context),
    m_urlTemplate(std::move(url)),
    m_options(std::move(options)) {

    if(m_urlTemplate.compare(0, 8, "function") == 0) {
        m_urlFunction = m_context.createFunction(m_urlTemplate);
    }

}

std::string NetworkDataSource::tileCoordinatesToQuadKey(const TileID &tile) {
    std::string quadKey;
    for (int i = tile.z; i > 0; i--) {
        char digit = '0';
        int mask = 1 << (i - 1);
        if ((tile.x & mask) != 0) {
            digit++;
        }
        if ((tile.y & mask) != 0) {
            digit++;
            digit++;
        }
        quadKey.push_back(digit);
    }
    return quadKey;
}

bool NetworkDataSource::urlHasTilePattern(const std::string &url) {
    return (url.find("{x}") != std::string::npos &&
            url.find("{y}") != std::string::npos &&
            url.find("{z}") != std::string::npos) ||
           (url.find("{q}") != std::string::npos) ||
           (url.find("{bbox}") != std::string::npos) ||
           (url.compare(0, 8, "function") == 0);
}

std::string NetworkDataSource::buildUrlForTile(const TileID& tile, const std::string& urlTemplate,
                                               const UrlOptions& options, int subdomainIndex) {

    std::string url = urlTemplate;

    size_t xPos = url.find("{x}");
    if (xPos != std::string::npos) {
        url.replace(xPos, 3, std::to_string(tile.x));
    }
    size_t yPos = url.find("{y}");
    if (yPos != std::string::npos) {
        int y = tile.y;
        int z = tile.z;
        if (options.isTms) {
            // Convert XYZ to TMS
            y = (1 << z) - 1 - tile.y;
        }
        url.replace(yPos, 3, std::to_string(y));
    }
    size_t zPos = url.find("{z}");
    if (zPos != std::string::npos) {
        url.replace(zPos, 3, std::to_string(tile.z));
    }
    if (subdomainIndex < options.subdomains.size()) {
        size_t sPos = url.find("{s}");
        if (sPos != std::string::npos) {
            url.replace(sPos, 3, options.subdomains[subdomainIndex]);
        }
    }
    size_t qPos = url.find("{q}");
    if (qPos != std::string::npos) {
        auto quadkey = tileCoordinatesToQuadKey(tile);
        url.replace(qPos, 3, quadkey);
    }
    // {bbox} is replaced with min_lng,min_lat,max_lng,max_lat for fetching from ArcGIS WMS server with CRS=CRS:84
    size_t bbPos = url.find("{bbox}");
    if (bbPos != std::string::npos) {
        auto bbox = MapProjection::tileBounds(tile);
        LngLat llmin = MapProjection::projectedMetersToLngLat(bbox.min);
        LngLat llmax = MapProjection::projectedMetersToLngLat(bbox.max);
        std::string bboxstr = fstring("%.8f,%.8f,%.8f,%.8f",
                 llmin.longitude, llmin.latitude, llmax.longitude, llmax.latitude);
        url.replace(bbPos, 6, bboxstr);
    }

    return url;
}

bool NetworkDataSource::loadTileData(std::shared_ptr<TileTask> task, TileTaskCb callback) {

    if (task->rawSource != this->level) {
        LOGE("NetworkDataSource must be last!");
        return false;
    }

    auto tileId = task->tileId();

    std::string urlstr;
    if (m_urlFunction >= 0) {
        auto lockedCtx = m_context.getJSContext();
        JSScope jsScope(*lockedCtx.ctx);
        auto jsX = jsScope.newNumber(tileId.x);
        auto jsY = jsScope.newNumber(tileId.y);
        auto jsZ = jsScope.newNumber(tileId.z);
        urlstr = jsScope.getFunctionResult(m_urlFunction, {jsX, jsY, jsZ}).toString();
    }

    Url url(buildUrlForTile(tileId, urlstr.empty() ? m_urlTemplate : urlstr, m_options, m_urlSubdomainIndex));

    if (!m_options.subdomains.empty()) {
        m_urlSubdomainIndex = (m_urlSubdomainIndex + 1) % m_options.subdomains.size();
    }

    //LOGTInit(">>> %s", task->tileId().toString().c_str());
    LOGTO(">>> Url request for %s %s", task->source()->name().c_str(), task->tileId().toString().c_str());
    UrlCallback onRequestFinish = [=](UrlResponse&& response) mutable {
        auto source = task->source();
        if (!source) {
            LOGW("URL callback for deleted TileSource '%s'", url.string().c_str());
            return;
        }
        //LOGT("<<< %s -- canceled:%d", task->tileId().toString().c_str(), task->isCanceled());
        LOGTO("<<< Url request for %s %s%s", task->source()->name().c_str(), task->tileId().toString().c_str(),
              task->isCanceled() ? " (canceled)" : "");

        if (task->isCanceled()) {
            return;
        }

        if (response.error) {
            LOGD("URL request '%s': %s", url.string().c_str(), response.error);

        } else if (!response.content.empty()) {
            auto& dlTask = static_cast<BinaryTileTask&>(*task);
            dlTask.rawTileData = std::make_shared<std::vector<char>>(std::move(response.content));
        }
        callback.func(std::move(task));
    };

    auto& dlTask = static_cast<BinaryTileTask&>(*task);
    dlTask.urlRequestHandle = m_context.getPlatform().startUrlRequest(url, m_options.httpOptions,
                                                                      std::move(onRequestFinish));
    dlTask.urlRequestStarted = true;

    return true;
}

void NetworkDataSource::cancelLoadingTile(TileTask& task) {
    auto& dlTask = static_cast<BinaryTileTask&>(task);
    if (dlTask.urlRequestStarted) {
        dlTask.urlRequestStarted = false;

        m_context.getPlatform().cancelUrlRequest(dlTask.urlRequestHandle);
    }
}

}
