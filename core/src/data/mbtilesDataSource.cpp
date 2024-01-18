#include "data/mbtilesDataSource.h"

#include "util/asyncWorker.h"
#include "util/zlibHelper.h"
#include "log.h"
#include "platform.h"
#include "util/url.h"

#include "sqlitepp.h"
#include "hash-library/md5.cpp"


namespace Tangram {

/**
 * The schema.sql used to set up an MBTiles Database.
 *
 * https://github.com/mapbox/node-mbtiles/blob/4bbfaf991969ce01c31b95184c4f6d5485f717c3/lib/schema.sql
 */
static const char* SCHEMA = R"SQL_ESC(BEGIN;

CREATE TABLE IF NOT EXISTS map (
   zoom_level INTEGER,
   tile_column INTEGER,
   tile_row INTEGER,
   tile_id TEXT
   -- grid_id TEXT
);

CREATE TABLE IF NOT EXISTS images (
    tile_data BLOB,
    tile_id TEXT
);

CREATE TABLE IF NOT EXISTS metadata (
    name TEXT,
    value TEXT
);

CREATE TABLE IF NOT EXISTS offline_tiles (
    tile_id TEXT,
    offline_id INTEGER
);

CREATE TABLE IF NOT EXISTS tile_last_access (
    tile_id TEXT,
    last_access INTEGER
);

CREATE UNIQUE INDEX IF NOT EXISTS map_index ON map (zoom_level, tile_column, tile_row);
CREATE UNIQUE INDEX IF NOT EXISTS images_id ON images (tile_id);
CREATE UNIQUE INDEX IF NOT EXISTS name ON metadata (name);
CREATE UNIQUE INDEX IF NOT EXISTS offline_index ON offline_tiles (tile_id, offline_id);
CREATE UNIQUE INDEX IF NOT EXISTS last_access_index ON tile_last_access (tile_id);

-- or we could use foreign keys: "tile_id REFERENCES images.tile_id ON DELETE CASCADE"
CREATE TRIGGER IF NOT EXISTS delete_tile AFTER DELETE ON images
BEGIN
    DELETE FROM map WHERE tile_id = OLD.tile_id;
    DELETE FROM tile_last_access WHERE tile_id = OLD.tile_id;
    --DELETE FROM offline_tiles WHERE tile_id = OLD.tile_id;
END;

CREATE VIEW IF NOT EXISTS tiles AS
    SELECT
        map.zoom_level AS zoom_level,
        map.tile_column AS tile_column,
        map.tile_row AS tile_row,
        images.tile_data AS tile_data,
        images.tile_id AS tile_id
    FROM map
    JOIN images ON images.tile_id = map.tile_id;

COMMIT;)SQL_ESC";


struct MBTilesQueries {
    SQLiteStmt getTileData;  // SELECT statement from tiles view
    SQLiteStmt putMap = nullptr;  // REPLACE INTO statement in map table
    SQLiteStmt putImage = nullptr;  // REPLACE INTO statement in images table
    // caching and offline maps
    SQLiteStmt getOffline = nullptr;
    SQLiteStmt putOffline = nullptr;
    SQLiteStmt delOffline = nullptr;
    SQLiteStmt delOfflineTiles = nullptr;
    SQLiteStmt delOldTiles = nullptr;
    SQLiteStmt getTileSizes = nullptr;
    SQLiteStmt getOfflineSize = nullptr;
    SQLiteStmt putLastAccess = nullptr;

    struct tag_cache {};
    MBTilesQueries(sqlite3* db);
    MBTilesQueries(sqlite3* db, tag_cache);
};

MBTilesQueries::MBTilesQueries(sqlite3* db) :
    getTileData(db, "SELECT tile_data FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?;") {}

MBTilesQueries::MBTilesQueries(sqlite3* db, tag_cache) :
    getTileData(db, "SELECT tile_data, images.tile_id FROM images JOIN map ON images.tile_id ="
        " map.tile_id WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?;"),
    putMap(db, "REPLACE INTO map (zoom_level, tile_column, tile_row, tile_id) VALUES (?, ?, ?, ?);"),
    putImage(db, "REPLACE INTO images (tile_id, tile_data) VALUES (?, ?);"),
    getOffline(db, "SELECT 1,tile_id FROM tiles WHERE zoom_level = ? AND tile_column = ? AND tile_row = ?;"),
    putOffline(db, "REPLACE INTO offline_tiles (tile_id, offline_id) VALUES (?, ?);"),
    delOffline(db, "DELETE FROM offline_tiles WHERE offline_id = ?;"),
    // note that rows in maps and tile_last_access will be deleted by trigger
    // a few potential alternatives are:
    // - omit "WHERE offline_id = ?1" ... seems like this might be slower though
    // - SELECT tile_id FROM offline_tiles GROUP BY tile_id HAVING MAX(offline_id) = ? AND MIN(offline_id) = ?
    // - SELECT tile_id FROM offline_tiles EXCEPT SELECT tile_id FROM offline_tiles WHERE offline_id <> ?;
    delOfflineTiles(db, "DELETE FROM images WHERE tile_id IN (SELECT tile_id FROM offline_tiles WHERE"
        " offline_id = ?1 AND tile_id NOT IN (SELECT tile_id FROM offline_tiles WHERE offline_id <> ?1));"),
    delOldTiles(db, "DELETE FROM images WHERE tile_id IN (SELECT tile_id FROM tile_last_access"
        " WHERE last_access < ? AND tile_id NOT IN (SELECT tile_id FROM offline_tiles));"),
    getTileSizes(db, "SELECT images.tile_id, ot.offline_id, tla.last_access, length(tile_data)"
        " FROM images LEFT JOIN tile_last_access AS tla ON images.tile_id = tla.tile_id"
        " LEFT JOIN offline_tiles AS ot ON images.tile_id = ot.tile_id;"),
    getOfflineSize(db, "SELECT sum(length(tile_data)) FROM images WHERE tile_id IN"
        " (SELECT tile_id FROM offline_tiles);"),
    putLastAccess(db, "REPLACE INTO tile_last_access (tile_id, last_access) VALUES"
        " (?, CAST(strftime('%s') AS INTEGER));") {}

MBTilesDataSource::MBTilesDataSource(Platform& _platform, std::string _name, std::string _path,
                                     std::string _mime, bool _cache, bool _offlineFallback)
    : m_name(_name),
      m_path(_path),
      m_mime(_mime),
      m_cacheMode(_cache),
      m_offlineMode(_offlineFallback),
      m_platform(_platform) {

    m_worker = std::make_unique<AsyncWorker>();

    openMBTiles();
}

MBTilesDataSource::~MBTilesDataSource() {
}

bool MBTilesDataSource::loadTileData(std::shared_ptr<TileTask> _task, TileTaskCb _cb) {

    // currently, DataSource.level is always zero because SceneLoader doesn't use DataSource::setNext()
    if (m_offlineMode) {
        if (_task->rawSource == this->level) {
            // Try next source
            _task->rawSource = next->level;
        }

        return loadNextSource(_task, _cb);
    }

    if (!m_db) { return false; }

    if (_task->rawSource == this->level) {

        m_worker->enqueue([this, _task, _cb](){
            if (_task->isCanceled()) { return; }  // task may have been canceled while in queue
            TileID tileId = _task->tileId();
            LOGTO(">>> DB query for %s %s", _task->source()->name().c_str(), tileId.toString().c_str());

            auto& task = static_cast<BinaryTileTask&>(*_task);
            auto tileData = std::make_unique<std::vector<char>>();
            // RasterTileTask::hasData() doesn't check if rawTileData is empty - it probably should, but
            //  let's not set rawTileData to empty vector, to match NetworkDataSource behavior
            getTileData(tileId, *tileData, task.offlineId);
            LOGTO("<<< DB query for %s %s%s", _task->source()->name().c_str(), tileId.toString().c_str(),
                  tileData->empty() ? " (not found)" : "");

            if (!tileData->empty()) {
                task.rawTileData = std::move(tileData);
                LOGV("loaded tile: %s, %d bytes", tileId.toString().c_str(), task.rawTileData->size());

                _cb.func(_task);

            } else if (next) {

                // Don't try this source again
                _task->rawSource = next->level;

                if (!loadNextSource(_task, _cb)) {
                    // Trigger TileManager update so that tile will be
                    // downloaded next time.
                    _task->setNeedsLoading(true);
                    m_platform.requestRender();
                }
            } else {
                LOGD("missing tile: %s", _task->tileId().toString().c_str());
                _cb.func(_task);  // added 2022-09-27 ... were doing this in loadNextSource, why not here?
            }
        });
        return true;
    }

    return loadNextSource(_task, _cb);
}

bool MBTilesDataSource::loadNextSource(std::shared_ptr<TileTask> _task, TileTaskCb _cb) {
    if (!next) { return false; }

    if (!m_db) {
        return next->loadTileData(_task, _cb);
    }

    // Intercept TileTaskCb to store result from next source.
    TileTaskCb cb{[this, _cb](std::shared_ptr<TileTask> _task) {

        if (_task->hasData()) {

            if (m_cacheMode) {
                m_worker->enqueue([this, _task](){

                        auto& task = static_cast<BinaryTileTask&>(*_task);

                        LOGD("store tile: %s, %d", _task->tileId().toString().c_str(), task.hasData());

                        storeTileData(_task->tileId(), *task.rawTileData, task.offlineId);
                    });
            }

            _cb.func(_task);

        } else if (m_offlineMode) {
            LOGD("try fallback tile: %s, %d", _task->tileId().toString().c_str());

            m_worker->enqueue([this, _task, _cb](){

                auto& task = static_cast<BinaryTileTask&>(*_task);
                task.rawTileData = std::make_shared<std::vector<char>>();

                getTileData(_task->tileId(), *task.rawTileData, task.offlineId);

                LOGV("loaded tile: %s, %d", _task->tileId().toString().c_str(), task.rawTileData->size());

                _cb.func(_task);

            });
        } else {
            LOGD("missing tile: %s", _task->tileId().toString().c_str());
            _cb.func(_task);
        }
    }};

    return next->loadTileData(_task, cb);
}

void MBTilesDataSource::openMBTiles() {

    auto mode = SQLITE_OPEN_FULLMUTEX;
    mode |= m_cacheMode ? (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE) : SQLITE_OPEN_READONLY;

    auto url = Url(m_path);
    auto path = url.path();
    const char* vfs = NULL;
    if (url.scheme() == "asset") {
        vfs = "ndk-asset";
        path.erase(path.begin()); // Remove leading '/'.
    }

    m_db = std::make_unique<SQLiteDB>();
    if (sqlite3_open_v2(path.c_str(), &m_db->db, mode, vfs) != SQLITE_OK) {
        LOGE("Unable to open SQLite database: %s - %s", m_path.c_str(), m_db->errMsg());
        m_db.reset();
        return;
    }
    LOG("SQLite database opened: %s", path.c_str());

    bool ok = testSchema(*m_db);
    if (ok) {
        if (m_cacheMode && !m_schemaOptions.isCache) {
            // TODO better description
            LOGE("Cannot cache to 'externally created' MBTiles database");
            // Run in non-caching mode
            m_cacheMode = false;
            return;
        }
    } else if (m_cacheMode) {

        // Setup the database by running the schema.sql.
        initSchema(*m_db, m_name, m_mime);

        ok = testSchema(*m_db);
        if (!ok) {
            LOGE("Unable to initialize MBTiles schema");
            m_db.reset();
            return;
        }
    } else {
        LOGE("Invalid MBTiles schema");
        m_db.reset();
        return;
    }

    if (m_schemaOptions.compression == Compression::unsupported) {
        m_db.reset();
        return;
    }

    m_queries = m_cacheMode ? std::make_unique<MBTilesQueries>(m_db->db, MBTilesQueries::tag_cache{})
        : std::make_unique<MBTilesQueries>(m_db->db);
}

/**
 * We check to see if the database has the MBTiles Schema.
 * Sets m_schemaOptions from metadata table
 *
 * @param _source A pointer to a the data source in which we will setup a db.
 * @return true if database contains MBTiles schema
 */
bool MBTilesDataSource::testSchema(SQLiteDB& db) {

    bool metadata = false, tiles = false;  //, grids = false, grid_data = false;

    db.stmt("SELECT name FROM sqlite_master WHERE type IN ('table', 'view')").exec([&](std::string name){
        // required
        if (name == "metadata") metadata = true;
        else if (name == "tiles") tiles = true;
        // optional
        //else if (name == "grids") grids = true;
        //else if (name == "grid_data") grid_data = true;
    });
    if (!metadata || !tiles) {
        LOGW("Missing MBTiles tables");
        return false;
    }

    db.stmt("SELECT value FROM metadata WHERE name = 'description';").exec([&](std::string desc){
        if (desc == "MBTiles tile container created by Tangram ES.") {
            m_schemaOptions.isCache = true;
        }
    });

    db.stmt("SELECT value FROM metadata WHERE name = 'compression';").exec([&](std::string cmpr){
        if (cmpr == "undefined" || cmpr == "unknown") {}
        else if (cmpr == "identity" || cmpr == "none") {
            m_schemaOptions.compression = Compression::identity;
        } else if (cmpr == "deflate" || cmpr == "gzip") {
            m_schemaOptions.compression = Compression::deflate;
        } else {
            LOGE("Unsupported MBTiles tile compression: %s", cmpr.c_str());
            m_schemaOptions.compression = Compression::unsupported;
        }
    });

    return true;
}

void MBTilesDataSource::initSchema(SQLiteDB& db, std::string _name, std::string _mimeType) {

    // Otherwise, we need to execute schema.sql to set up the db with the right schema.
    db.exec(SCHEMA);
    // Fill in metadata table.
    // https://github.com/pnorman/mbtiles-spec/blob/2.0/2.0/spec.md#content
    // https://github.com/mapbox/mbtiles-spec/pull/46
    SQLiteStmt stmt = db.stmt("REPLACE INTO metadata (name, value) VALUES (?, ?);");
    // name, type, version, description, format, compression
    stmt.bind("name", _name).exec();
    stmt.bind("type", "baselayer").exec();
    stmt.bind("version", "1").exec();
    stmt.bind("description", "MBTiles tile container created by Tangram ES.").exec();
    stmt.bind("format", _mimeType).exec();
    // Compression not yet implemented - no gain for raster tiles (png or jpg); gziping vector .mbtiles
    //  gave around 40% size reduction
    // http://www.iana.org/assignments/http-parameters/http-parameters.xhtml#content-coding
    // identity means no compression
    stmt.bind("compression", "identity").exec();
}

bool MBTilesDataSource::getTileData(const TileID& _tileId, std::vector<char>& _data, int offlineId) {

    if (offlineId && !m_cacheMode) {
        LOGE("Offline tiles cannot be created: database is read-only!");
        return false;
    }
    // MBTiles uses TMS (tile row incr. south to north) while TileID is WMTS (tile row incr. north to south)
    int z = _tileId.z;
    int y = (1 << z) - 1 - _tileId.y;

    // offlineId > 0 indicates request to set offline_id; not necessary to read data
    if (offlineId > 0) {
        return m_queries->getOffline.bind(z, _tileId.x, y).exec([&](int, const char* tileid){
            m_queries->putOffline.bind(tileid, std::abs(offlineId)).exec();
            _data.push_back('\0');  // make TileTask::hasData() true
        });
    }

    return m_queries->getTileData.bind(z, _tileId.x, y).exec([&](sqlite3_stmt* stmt){
        const char* blob = (const char*) sqlite3_column_blob(stmt, 0);
        const int length = sqlite3_column_bytes(stmt, 0);
        std::string tileid = m_cacheMode ? (const char*)sqlite3_column_text(stmt, 1) : "";

        if ((m_schemaOptions.compression == Compression::undefined) ||
            (m_schemaOptions.compression == Compression::deflate)) {

            if (zlib_inflate(blob, length, _data) != 0) {
                if (m_schemaOptions.compression == Compression::undefined) {
                    _data.resize(length);
                    memcpy(_data.data(), blob, length);
                } else {
                    LOGW("Invalid deflate compression");
                }
            }
        } else {
            _data.resize(length);
            memcpy(_data.data(), blob, length);
        }
        if (offlineId) {
            m_queries->putOffline.bind(tileid, std::abs(offlineId)).exec();
        }
        if (m_cacheMode) {
            m_queries->putLastAccess.bind(tileid).exec();
        }
    });
}

void MBTilesDataSource::storeTileData(const TileID& _tileId, const std::vector<char>& _data, int offlineId) {
    int z = _tileId.z;
    int y = (1 << z) - 1 - _tileId.y;

    const char* data = _data.data();
    size_t size = _data.size();

    /**
     * We create an MD5 of the raw tile data. The MD5 functions as a hash
     * between the map and images tables. With this, tiles with duplicate
     * data will join to a single entry in the images table.
     */
    MD5 md5;
    std::string md5id = md5(data, size);

    m_queries->putMap.bind(z, _tileId.x, y, md5id).exec();
    sqlite3_bind_text(m_queries->putImage.stmt, 1, md5id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_blob(m_queries->putImage.stmt, 2, data, size, SQLITE_STATIC);
    m_queries->putImage.exec();

    m_platform.notifyStorage(size, 0);  //offlineId ? size : 0);
    if (offlineId) {
        m_queries->putOffline.bind(md5id, std::abs(offlineId)).exec();
    }
}

void MBTilesDataSource::deleteOfflineMap(int offlineId, bool delTiles) {
    if (delTiles) {
        int totchanges = m_db->totalChanges();
        m_queries->delOfflineTiles.bind(offlineId).exec();
        if (m_db->totalChanges() - totchanges > 32)
            m_db->exec("VACUUM;");
    }
    m_queries->delOffline.bind(offlineId).exec();
}

void MBTilesDataSource::deleteOldTiles(int cutoff) {
    int totchanges = m_db->totalChanges();
    m_queries->delOldTiles.bind(cutoff).exec();
    if (m_db->totalChanges() - totchanges > 32)  // SqliteCpp doesn't wrap sqlite3_changes()
        m_db->exec("VACUUM;");
}

void MBTilesDataSource::getTileSizes(std::function<void(int, int, int)> cb) {
    m_queries->getTileSizes.exec([&](const char*, int ofl_id, int ts, int size){ cb(ofl_id, ts, size); });
}

int64_t MBTilesDataSource::getOfflineSize() {
    int64_t size = 0;
    m_queries->getOfflineSize.exec([&](int64_t s){ size = s; });
    return size;
}

sqlite3* MBTilesDataSource::dbHandle()
{
  return m_db->db;
}

}
