#include "offlinemaps.h"
#include "mapsapp.h"
#include "mapsearch.h"
#include "util.h"
//#include "imgui.h"
#include <deque>
//#include "sqlite3/sqlite3.h"
#include "sqlitepp.h"
// "private" headers
#include "scene/scene.h"
#include "data/mbtilesDataSource.h"
#include "data/networkDataSource.h"

#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"
#include "nfd.h"

#include "mapsources.h"
#include "mapwidgets.h"

static bool runOfflineWorker = false;
static std::unique_ptr<std::thread> offlineWorker;
static Semaphore semOfflineWorker(1);
static const char* polylineStyle = "{ style: lines, color: red, width: 4px, order: 5000 }";  //interactive: true,


// Offline maps
// - initial discussion https://github.com/tangrams/tangram-es/issues/931

struct OfflineSourceInfo
{
  std::string name;
  std::string cacheFile;
  std::string url;
  Tangram::UrlOptions urlOptions;
  int maxZoom;
  YAML::Node searchData;
};

struct OfflineMapInfo
{
  int id;
  LngLat lngLat00, lngLat11;
  int zoom, maxZoom;
  std::vector<OfflineSourceInfo> sources;
  bool canceled;
};

class OfflineDownloader
{
public:
    OfflineDownloader(Platform& _platform, const OfflineMapInfo& ofl, const OfflineSourceInfo& src);
    ~OfflineDownloader();
    size_t remainingTiles() const { return m_queued.size() + m_pending.size(); }
    bool fetchNextTile();
    void cancel();
    std::string name;
    int totalTiles = 0;

private:
    void tileTaskCallback(std::shared_ptr<TileTask> _task);

    int offlineId;
    int srcMaxZoom;
    int64_t offlineSize;
    bool canceled = false;
    std::deque<TileID> m_queued;
    std::vector<TileID> m_pending;
    std::mutex m_mutexQueue;
    std::unique_ptr<Tangram::MBTilesDataSource> mbtiles;
    std::vector<SearchData> searchData;
};

static MapsOffline* mapsOfflineInst = NULL;  // for updateProgress()
static int maxOfflineDownloads = 4;
static std::deque<OfflineMapInfo> offlinePending;
static std::mutex mutexOfflineQueue;
static std::vector<std::unique_ptr<OfflineDownloader>> offlineDownloaders;

static void offlineDLStep()
{
  std::unique_lock<std::mutex> lock(mutexOfflineQueue);

  Platform& platform = *MapsApp::platform;
  while(!offlinePending.empty()) {
    if(offlineDownloaders.empty()) {
      auto& dl = offlinePending.front();
      for(auto& source : dl.sources)
        offlineDownloaders.emplace_back(new OfflineDownloader(platform, dl, source));
    }
    while(!offlineDownloaders.empty()) {
      // DB access (and this network requests for missing tiles) are async, so activeUrlRequests() won't update
      int nreq = maxOfflineDownloads - int(platform.activeUrlRequests());
      while(nreq > 0 && offlineDownloaders.back()->fetchNextTile()) --nreq;
      if(nreq <= 0 || offlineDownloaders.back()->remainingTiles())
        return;  // m_queued empty, m_pending not empty
      LOGD("completed offline tile downloads for layer %s", offlineDownloaders.back()->name.c_str());
      offlineDownloaders.pop_back();
    }
    LOG("completed offline tile downloads for map %d", offlinePending.front().id);

    MapsApp::runOnMainThread([id=offlinePending.front().id, canceled=offlinePending.front().canceled](){
      mapsOfflineInst->downloadCompleted(id, canceled);
    });

    offlinePending.pop_front();
  }
  platform.onUrlRequestsThreshold = nullptr;  // all done!
}

static void offlineDLMain()
{
  semOfflineWorker.wait();
  while(runOfflineWorker) {
    offlineDLStep();
    semOfflineWorker.wait();
  }
}

#include "md5.h"

static void udf_md5(sqlite3_context* context, int argc, sqlite3_value** argv)
{
  if(argc != 1) {
    sqlite3_result_error(context, "sqlite md5() - Invalid number of arguments (1 required).", -1);
    return;
  }
  MD5 md5;
  int len = sqlite3_value_bytes(argv[0]);
  std::string hash = md5(sqlite3_value_blob(argv[0]), len);
  sqlite3_result_text(context, hash.c_str(), -1, SQLITE_TRANSIENT);
}

OfflineDownloader::OfflineDownloader(Platform& _platform, const OfflineMapInfo& ofl, const OfflineSourceInfo& src)
{
  mbtiles = std::make_unique<Tangram::MBTilesDataSource>(_platform, src.name, src.cacheFile, "", true);
  name = src.name + "-" + std::to_string(ofl.id);
  offlineId = ofl.id;
  searchData = MapsSearch::parseSearchFields(src.searchData);
  offlineSize = mbtiles->getOfflineSize();
  srcMaxZoom = std::min(ofl.maxZoom, src.maxZoom);

  // SQL import?
  if(src.url.substr(0, 5) == "BEGIN") {
    if(sqlite3_create_function(mbtiles->dbHandle(), "md5", 1, SQLITE_UTF8, 0, udf_md5, 0, 0) != SQLITE_OK)
      LOGE("SQL error creading md5() function: %s", sqlite3_errmsg(mbtiles->dbHandle()));
    else if(!SQLiteStmt(mbtiles->dbHandle(), src.url).exec())
      LOGE("SQL error importing mbtiles: %s", sqlite3_errmsg(mbtiles->dbHandle()));
    else if(!searchData.empty()) {
      const char* newtilesSql = "SELECT tile_column, tile_row FROM map JOIN offline_tiles"
          " ON map.tile_id = offline_tiles.tile_id WHERE offline_id = ? AND zoom_level = ?";
      SQLiteStmt(mbtiles->dbHandle(), newtilesSql).bind(offlineId, srcMaxZoom).exec([&](int x, int y){
        m_queued.emplace_back(x, y, srcMaxZoom);
      });
    }
  }
  else {
    mbtiles->next = std::make_unique<Tangram::NetworkDataSource>(_platform, src.url, src.urlOptions);
    // if zoomed past srcMaxZoom, download tiles at srcMaxZoom
    for(int z = std::min(ofl.zoom, srcMaxZoom); z <= srcMaxZoom; ++z) {
      TileID tile00 = lngLatTile(ofl.lngLat00, z);
      TileID tile11 = lngLatTile(ofl.lngLat11, z);
      for(int x = tile00.x; x <= tile11.x; ++x) {
        for(int y = tile11.y; y <= tile00.y; ++y)  // note y tile index incr for decr latitude
          m_queued.emplace_back(x, y, z);
      }
    }
    // queue all z3 tiles so user sees world map when zooming out
    if(ofl.zoom > 3) {  // && cfg->Bool("offlineWorldMap")
      for(int x = 0; x < 8; ++x) {
        for(int y = 0; y < 8; ++y)
          m_queued.emplace_back(x, y, 3);
      }
    }
  }
  totalTiles = m_queued.size();
}

OfflineDownloader::~OfflineDownloader()
{
  MapsApp::platform->notifyStorage(0, mbtiles->getOfflineSize() - offlineSize);
}

void OfflineDownloader::cancel()
{
  std::unique_lock<std::mutex> lock(m_mutexQueue);
  m_queued.clear();
  canceled = true;  //m_pending.clear();
}

bool OfflineDownloader::fetchNextTile()
{
  std::unique_lock<std::mutex> lock(m_mutexQueue);
  if(m_queued.empty()) return false;
  auto task = std::make_shared<BinaryTileTask>(m_queued.front(), nullptr);
  // prevent redundant write to offline_tiles table if importing from mbtiles file
  bool needdata = !searchData.empty() && task->tileId().z == srcMaxZoom;
  task->offlineId = mbtiles->next ? (needdata ? -offlineId : offlineId) : 0;
  m_pending.push_back(m_queued.front());
  m_queued.pop_front();
  lock.unlock();
  TileTaskCb cb{[this](std::shared_ptr<TileTask> _task) { tileTaskCallback(_task); }};
  mbtiles->loadTileData(task, cb);
  LOGD("%s: requested download of offline tile %s", name.c_str(), task->tileId().toString().c_str());
  return true;
}

void OfflineDownloader::tileTaskCallback(std::shared_ptr<TileTask> task)
{
  std::unique_lock<std::mutex> lock(m_mutexQueue);

  TileID tileId = task->tileId();
  auto pendingit = std::find(m_pending.begin(), m_pending.begin(), tileId);
  if(pendingit == m_pending.end()) {
    LOGW("Pending tile entry not found for tile!");
    return;
  }
  // put back in queue (at end) on failure
  if(canceled) {}
  else if(!task->hasData()) {
    m_queued.push_back(*pendingit);
    LOGW("%s: download of offline tile %s failed - will retry", name.c_str(), task->tileId().toString().c_str());
  } else {
    if(!searchData.empty() && task->tileId().z == srcMaxZoom)
      MapsSearch::indexTileData(task.get(), offlineId, searchData);
    LOGD("%s: completed download of offline tile %s", name.c_str(), task->tileId().toString().c_str());
  }
  m_pending.erase(pendingit);

  MapsApp::runOnMainThread([](){
    mapsOfflineInst->updateProgress();
  });

  semOfflineWorker.post();
}

void MapsOffline::saveOfflineMap(int mapid, LngLat lngLat00, LngLat lngLat11, int maxZoom)
{
  Map* map = app->map;
  std::unique_lock<std::mutex> lock(mutexOfflineQueue);
  // don't load tiles outside visible region at any zoom level (as using TileID::getChild() recursively
  //  would do - these could potentially outnumber the number of desired tiles!)
  double heightkm = lngLatDist(lngLat00, LngLat(lngLat00.longitude, lngLat11.latitude));
  double widthkm = lngLatDist(lngLat11, LngLat(lngLat00.longitude, lngLat11.latitude));
  int zoom = std::round(MapProjection::zoomAtMetersPerPixel( std::min(heightkm, widthkm)/MapProjection::tileSize() ));
  //int zoom = int(map->getZoom());
  // queue offline downloads
  offlinePending.push_back({mapid, lngLat00, lngLat11, zoom, maxZoom, {}, false});
  auto& tileSources = map->getScene()->tileSources();
  for(auto& src : tileSources) {
    auto& info = src->offlineInfo();
    if(info.cacheFile.empty())
      LOGE("Cannot save offline tiles for source %s - no cache file specified", src->name().c_str());
    else {
      offlinePending.back().sources.push_back(
          {src->name(), info.cacheFile, info.url, info.urlOptions, src->maxZoom(), {}});
      if(!src->isRaster())
        Tangram::YamlPath("global.search_data").get(map->getScene()->config(), offlinePending.back().sources.back().searchData);
    }
  }

  MapsApp::platform->onUrlRequestsThreshold = [&](){ semOfflineWorker.post(); };  //onUrlClientIdle;
  MapsApp::platform->urlRequestsThreshold = maxOfflineDownloads - 1;
  semOfflineWorker.post();
  runOfflineWorker = true;
  if(!offlineWorker)
    offlineWorker = std::make_unique<std::thread>(offlineDLMain);
}

MapsOffline::~MapsOffline()
{
  if(offlineWorker) {
    runOfflineWorker = false;
    semOfflineWorker.post();
    offlineWorker->join();
  }
}

int MapsOffline::numOfflinePending() const
{
  return offlinePending.size();
}

bool MapsOffline::cancelDownload(int mapid)
{
  std::unique_lock<std::mutex> lock(mutexOfflineQueue);
  if(offlinePending.front().id == mapid) {
    offlinePending.front().canceled = true;
    for(auto& dl : offlineDownloaders)
      dl->cancel();
    return false;
  }
  offlinePending.erase(std::remove_if(offlinePending.begin(), offlinePending.end(),
      [mapid](auto a){ return a.id == mapid; }), offlinePending.end());
  return true;
}

static void deleteOfflineMap(int mapid)
{
  int64_t offlineSize = 0;
  FSPath cachedir(MapsApp::baseDir, "cache");
  for(auto& file : lsDirectory(cachedir)) {
    //for(auto& src : mapSources) ... this doesn't work because cache file may be specified in scene yaml
    //std::string cachename = src.second["cache"] ? src.second["cache"].Scalar() : src.first.Scalar();
    //std::string cachefile = app->baseDir + "cache/" + cachename + ".mbtiles";
    //if(cachename == "false" || !FSPath(cachefile).exists()) continue;
    FSPath cachefile = cachedir.child(file);
    if(cachefile.extension() != "mbtiles") continue;
    auto s = std::make_unique<Tangram::MBTilesDataSource>(
        *MapsApp::platform, cachefile.baseName(), cachefile.path, "", true);
    offlineSize -= s->getOfflineSize();
    s->deleteOfflineMap(mapid);
    offlineSize += s->getOfflineSize();
  }
  MapsApp::platform->notifyStorage(0, offlineSize);  // this can trigger cache shrink, so wait until all sources processed

  DB_exec(MapsApp::bkmkDB, "DELETE FROM offlinemaps WHERE mapid = ?;", NULL,
      [&](sqlite3_stmt* stmt1){ sqlite3_bind_int(stmt1, 1, mapid); });
}

void MapsOffline::downloadCompleted(int id, bool canceled)
{
  if(canceled) {
    deleteOfflineMap(id);
  }
  else {
    DB_exec(MapsApp::bkmkDB, "UPDATE offlinemaps SET done = 1 WHERE mapid = ?;", NULL,
        [&](sqlite3_stmt* stmt){ sqlite3_bind_int(stmt, 1, id); });
  }
  populateOffline();
}

void MapsOffline::resumeDownloads()
{
  // caller should restore map source if necessary
  //std::string prevsrc = app->mapsSources->currSource;
  const char* query = "SELECT mapid, lng0,lat0,lng1,lat1, maxzoom, source FROM offlinemaps WHERE done = 0 ORDER BY timestamp;";
  DB_exec(app->bkmkDB, query, [&](sqlite3_stmt* stmt){
    int mapid = sqlite3_column_int(stmt, 0);
    double lng0 = sqlite3_column_double(stmt, 1), lat0 = sqlite3_column_double(stmt, 2);
    double lng1 = sqlite3_column_double(stmt, 3), lat1 = sqlite3_column_double(stmt, 4);
    int maxZoom = sqlite3_column_int(stmt, 5);
    std::string sourcestr = (const char*)(sqlite3_column_text(stmt, 6));

    app->mapsSources->rebuildSource(sourcestr);
    saveOfflineMap(mapid, LngLat(lng0, lat0), LngLat(lng1, lat1), maxZoom);

    LOG("Resuming offline map download for source %s", sourcestr.c_str());
  });
}

bool MapsOffline::importFile(std::string destsrc, std::string srcpath)
{
  if(destsrc != app->mapsSources->currSource) {
    bool old_async = std::exchange(app->load_async, false);
    // loading source will ensure mbtiles cache is created if enabled for source
    app->mapsSources->rebuildSource(destsrc);
    app->load_async = old_async;
  }

  auto tileSource = app->map->getScene()->tileSources().front();
  std::string destpath = tileSource->offlineInfo().cacheFile;
  if(destpath.empty())
    destpath = tileSource->offlineInfo().url;
  if(destpath.empty() || Url::getPathExtension(destpath) != "mbtiles") {
    MapsApp::messageBox("Import map", "Cannot import to selected source: no cache file found", {"OK"});
    return false;
  }

  SQLiteDB srcDB;
  if(sqlite3_open_v2(srcpath.c_str(), &srcDB.db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
    MapsApp::messageBox("Import map", fstring("Cannot import from %s: cannot open file", srcpath.c_str()), {"OK"});
    return false;
  }
  //if(!srcDB.exec(fstring("ATTACH DATABASE %s AS dest;", destpath.c_str()))) {
  //  MapsApp::messageBox("Import map",
  //      fstring("Cannot import to selected source: attach database failed - %s", srcDB.errMsg()), {"OK"});
  //  return false;
  //}

  // mbtiles cache does not set json metadata field, so we cannot compare that
  //bool formatConflict = false;
  //srcDB.stmt("SELECT s.value, d.value FROM metadata AS s LEFT JOIN dest.metadata AS d ON s.name = d.name "
  //    " WHERE s.name = 'format';").exec([&](std::string key, std::string sval, std::string dval) {
  //  if(!dval.empty() && sval != dval)
  //    formatConflict = true;
  //});
  //if(formatConflict) {
  //  MapsApp::messageBox("Import map", "Cannot import to selected source: tile format does not match!", {"OK"});
  //  return false;
  //}

  std::string importSql;
  int offlineId = int(time(NULL));
  bool hasTiles = false, hasMap = false, hasImages = false;
  srcDB.stmt("SELECT name FROM sqlite_master WHERE type IN 'table';")  //('table', 'view')
      .exec([&](std::string tblname){
    if(tblname == "map") hasMap = true;
    else if(tblname == "images") hasImages = true;
    else if(tblname == "tiles") hasTiles = true;
  });
  if(hasTiles) {
    //if(sqlite3_create_function(srcDB.db, "md5", 1, SQLITE_UTF8, 0, udf_md5, 0, 0) != SQLITE_OK) {
    //  MapsApp::messageBox("Import map", fstring("Import failed with SQL error: %s", srcDB.errMsg()), {"OK"});
    //  return false;
    //}
    // CREATE TRIGGER IF NOT EXISTS insert_tile INSTEAD OF INSERT ON tiles
    // BEGIN
    //   INSERT INTO map VALUES(NEW.zoom_level, NEW.tile_column, NEW.tile_row, md5(NEW.tile_data));
    //   INSERT INTO images VALUES(NEW.tile_data, SELECT tile_id FROM map WHERE
    //     map.zoom_level = NEW.zoom_level AND map.tile_column = NEW.tile_column AND map.tile_row = NEW.tile_row);
    //   -- rowid == last_insert_rowid();
    // END;
    const char* query = R"#(BEGIN;
      ATTACH DATABASE %s AS src;
      REPLACE INTO map SELECT zoom_level, tile_column, tile_row, md5(tile_data) FROM src.tiles;
      DELETE FROM images WHERE tile_id NOT IN (SELECT tile_id FROM map);  -- delete orphaned tiles
      REPLACE INTO images SELECT s.tile_data, map.tile_id FROM src.tiles AS s JOIN map ON
        s.zoom_level = map.zoom_level AND s.tile_column = map.tile_column AND s.tile_row = map.tile_row;
      REPLACE INTO offline_tiles SELECT map.tile_id, %d FROM src.tiles AS s JOIN map ON
        s.zoom_level = map.zoom_level AND s.tile_column = map.tile_column AND s.tile_row = map.tile_row;
      DETACH DATABASE src;
      COMMIT;)#";
    importSql = fstring(query, srcpath.c_str(), offlineId);
    //if(!srcDB.exec(fstring(query, offlineId))) {
    //  MapsApp::messageBox("Import map", fstring("Import failed with SQL error: %s", srcDB.errMsg()), {"OK"});
    //  return false;
    //}
  }
  else if(hasMap && hasImages) {
    const char* query = R"#(BEGIN;
      ATTACH DATABASE %s AS src;
      REPLACE INTO map SELECT * FROM src.map;
      DELETE FROM images WHERE tile_id NOT IN (SELECT tile_id FROM map);  -- delete orphaned tiles
      REPLACE INTO images SELECT * FROM src.images;
      REPLACE INTO offline_tiles SELECT tile_id, %d FROM src.map;
      DETACH DATABASE src;
      COMMIT;)#";
    importSql = fstring(query, srcpath.c_str(), offlineId);
    //if(!srcDB.exec(fstring(query, offlineId))) {
    //  MapsApp::messageBox("Import map", fstring("Import failed with SQL error: %s", srcDB.errMsg()), {"OK"});
    //  return false;
    //}
  }
  else{
    MapsApp::messageBox("Import map", fstring("Import failed: unknown MBTiles schema in %s", srcpath.c_str()), {"OK"});
    return false;
  }

  LngLat lngLat00, lngLat11;
  int maxZoom = 0;
  const char* boundsSql = "SELECT min(tile_row), max(tile_row), min(tile_column), max(tile_column),"
      " max(zoom_level) FROM tiles WHERE zoom_level = (SELECT max(zoom_level) FROM tiles);";
  srcDB.stmt(boundsSql).exec([&](int min_row, int max_row, int min_col, int max_col, int max_zoom){
    maxZoom = max_zoom;
    lngLat00 = MapProjection::projectedMetersToLngLat(
        MapProjection::tileSouthWestCorner(TileID(min_col, max_row, max_zoom)));
    lngLat11 = MapProjection::projectedMetersToLngLat(
        MapProjection::tileSouthWestCorner(TileID(max_col+1, min_row-1, max_zoom)));
  });

  std::string maptitle = FSPath(srcpath).baseName();
  const char* query = "INSERT INTO offlinemaps (mapid,lng0,lat0,lng1,lat1,maxzoom,source,title) VALUES (?,?,?,?,?,?,?,?);";
  SQLiteStmt(app->bkmkDB, query).bind(offlineId, lngLat00.longitude, lngLat00.latitude,
      lngLat11.longitude, lngLat11.latitude, maxZoom, app->mapsSources->currSource, maptitle).exec();

  std::unique_lock<std::mutex> lock(mutexOfflineQueue);
  // queue offline downloads
  offlinePending.push_back({offlineId, lngLat00, lngLat11, 0, maxZoom, {}, false});
  offlinePending.back().sources.push_back(
  {tileSource->name(), destpath, importSql, {}, tileSource->maxZoom(), {}});
  if(!tileSource->isRaster())
    Tangram::YamlPath("global.search_data").get(
        app->map->getScene()->config(), offlinePending.back().sources.back().searchData);

  semOfflineWorker.post();
  runOfflineWorker = true;
  if(!offlineWorker)
    offlineWorker = std::make_unique<std::thread>(offlineDLMain);

  return true;
}

// GUI

void MapsOffline::updateProgress()
{
  if(!offlinePanel->isVisible()) return;
  std::unique_lock<std::mutex> lock(mutexOfflineQueue);

  for(Widget* item : offlineContent->select(".listitem")) {
    int mapid = item->node->getIntAttr("__mapid");
    for(size_t ii = 0; ii < offlinePending.size(); ++ii) {
      if(offlinePending[ii].id == mapid) {
        if(offlinePending[ii].canceled)
          item->selectFirst(".detail-text")->setText("Canceling...");
        else if(ii == 0) {
          int total = 0, remaining = 0;
          for(auto& dl : offlineDownloaders) {
            total += dl->totalTiles;
            remaining += dl->remainingTiles();
          }
          item->selectFirst(".detail-text")->setText(fstring("%d/%d tiles downloaded", total - remaining, total).c_str());
        }
        else
          item->selectFirst(".detail-text")->setText("Download pending");
        item->selectFirst(".delete-btn")->setText("Cancel");
        break;
      }
    }
  }
}

void MapsOffline::populateOffline()
{
  app->gui->deleteContents(offlineContent, ".listitem");

  const char* query = "SELECT mapid, lng0,lat0,lng1,lat1, source, title, timestamp FROM offlinemaps ORDER BY timestamp DESC;";
  SQLiteStmt(app->bkmkDB, query).exec([&](int mapid, double lng0, double lat0, double lng1, double lat1,
      std::string sourcestr, std::string titlestr, int timestamp){
    auto srcinfo = app->mapsSources->mapSources[sourcestr];

    std::string detail = srcinfo ? srcinfo["title"].Scalar() : sourcestr;
    detail.append(u8" \u2022 ").append(ftimestr("%FT%H.%M.%S", timestamp));
    Button* item = createListItem(MapsApp::uiIcon("fold-map"), titlestr.c_str(), detail.c_str());
    item->node->setAttr("__mapid", mapid);
    item->onClicked = [=](){
      bool checked = !item->isChecked();
      for(Widget* w : offlineContent->select(".listitem"))
        static_cast<Button*>(w)->setChecked(checked && static_cast<Button*>(w) == item);
      if(!checked) {
        app->map->markerSetVisible(rectMarker, false);
        return;
      }
      // show bounds of offline region on map
      LngLat bounds[5] = {{lng0, lat0}, {lng0, lat1}, {lng1, lat1}, {lng1, lat0}, {lng0, lat0}};
      if(!rectMarker)
        rectMarker = app->map->markerAdd();
      else
        app->map->markerSetVisible(rectMarker, true);
      app->map->markerSetStylingFromString(rectMarker, polylineStyle);
      app->map->markerSetPolyline(rectMarker, bounds, 5);
      app->map->setCameraPositionEased(app->map->getEnclosingCameraPosition(bounds[0], bounds[2], {32}), 0.5f);
      if(app->mapsSources->currSource != sourcestr)
        app->mapsSources->rebuildSource(sourcestr);
    };

    Button* deleteBtn = new Button(item->containerNode()->selectFirst(".delete-btn"));
    deleteBtn->onClicked = [=](){
      if(rectMarker)
        app->map->markerSetVisible(rectMarker, false);
      if(cancelDownload(mapid)) {
        deleteOfflineMap(mapid);
        populateOffline();
      }
      else
        updateProgress();
    };

    offlineContent->addWidget(item);
  });
  updateProgress();
}

Widget* MapsOffline::createPanel()
{
  mapsOfflineInst = this;
  // should we include zoom? total bytes?
  DB_exec(app->bkmkDB, "CREATE TABLE IF NOT EXISTS offlinemaps(mapid INTEGER PRIMARY KEY,"
      " lng0 REAL, lat0 REAL, lng1 REAL, lat1 REAL, maxzoom INTEGER, source TEXT, title TEXT,"
      " done INTEGER DEFAULT 0, timestamp INTEGER DEFAULT (CAST(strftime('%s') AS INTEGER)));");

  TextEdit* titleEdit = createTitledTextEdit("Title");
  SpinBox* maxZoomSpin = createSpinBox(13, 1, 1, 20, "%.0f");
  Widget* maxZoomRow = createTitledRow("Max zoom", maxZoomSpin);

  auto downloadFn = [=](){
    LngLat lngLat00, lngLat11;
    app->getMapBounds(lngLat00, lngLat11);
    int offlineId = int(time(NULL));
    int maxZoom = int(maxZoomSpin->value());
    saveOfflineMap(offlineId, lngLat00, lngLat11, maxZoom);
    const char* query = "INSERT INTO offlinemaps (mapid,lng0,lat0,lng1,lat1,maxzoom,source,title) VALUES (?,?,?,?,?,?,?,?);";
    SQLiteStmt(app->bkmkDB, query).bind(offlineId, lngLat00.longitude, lngLat00.latitude,
        lngLat11.longitude, lngLat11.latitude, maxZoom, app->mapsSources->currSource, titleEdit->text()).exec();
    populateOffline();
    auto item = static_cast<Button*>(offlineContent->selectFirst(".listitem"));
    if(item) item->onClicked();
  };

  Widget* downloadPanel = createInlineDialog({titleEdit, maxZoomRow}, "Download", downloadFn);  //createColumn();

  Button* openBtn = createToolbutton(MapsApp::uiIcon("open-folder"), "Install Offline Map");
  openBtn->onClicked = [=](){
    nfdchar_t* outPath;
    nfdfilteritem_t filterItem[1] = { { "MBTiles files", "mbtiles" } };
    nfdresult_t result = NFD_OpenDialog(&outPath, filterItem, 1, NULL);
    if(result != NFD_OKAY)
      return;

    std::string srcpath(outPath);
    std::vector<std::string> layerKeys;
    std::vector<std::string> layerTitles;
    for(const auto& src : app->mapsSources->mapSources) {
      std::string type = src.second["type"].Scalar();
      if(type == "Raster" || type == "Vector") {
        layerKeys.push_back(src.first.Scalar());
        layerTitles.push_back(src.second["title"].Scalar());
      }
    }
    if(!selectDestDialog)
      selectDestDialog.reset(createSelectDialog("Choose source", MapsApp::uiIcon("layers")));
    selectDestDialog->addItems(layerTitles);
    selectDestDialog->onSelected = [=](int idx){
      importFile(layerKeys[idx], srcpath);
    };
  };

  Button* saveBtn = createToolbutton(MapsApp::uiIcon("download"), "Save Offline Map");
  saveBtn->onClicked = [=](){
    titleEdit->setText(ftimestr("%FT%H.%M.%S").c_str());
    titleEdit->selectAll();

    int maxZoom = 0;
    auto& tileSources = app->map->getScene()->tileSources();
    for(auto& src : tileSources)
      maxZoom = std::max(maxZoom, src->maxZoom());
    maxZoomSpin->setLimits(1, maxZoom);
    if(maxZoomSpin->value() > maxZoom)
      maxZoomSpin->setValue(maxZoom);

    downloadPanel->setVisible(true);
  };

  offlineContent = createColumn();
  auto toolbar = app->createPanelHeader(MapsApp::uiIcon("offline"), "Offline Maps");
  toolbar->addWidget(openBtn);
  toolbar->addWidget(saveBtn);
  offlinePanel = app->createMapPanel(toolbar, offlineContent, NULL, false);

  offlinePanel->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == MapsApp::PANEL_CLOSED) {
      if(rectMarker)
        app->map->markerSetVisible(rectMarker, false);
    }
    return false;
  });

  offlineContent->addWidget(downloadPanel);

  Button* offlineBtn = createToolbutton(MapsApp::uiIcon("offline"), "Offline Maps");
  offlineBtn->onClicked = [this](){
    app->showPanel(offlinePanel, true);
    populateOffline();
  };
  return offlineBtn;
}
