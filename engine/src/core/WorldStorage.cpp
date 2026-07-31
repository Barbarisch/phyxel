#include "core/WorldStorage.h"
#include "core/LodBlobCodec.h"
#include "core/LodPyramidService.h"
#include "core/Chunk.h"
#include "core/ChunkBlobCodec.h"
#include "core/Cube.h"
#include "core/Subcube.h"
#include "utils/Logger.h"
#include <iostream>
#include <filesystem>
#include <unordered_set>

#ifdef ENABLE_WORLD_STORAGE
#include <sqlite3.h>
#endif

// Only compile SQLite functionality if available
// C3.2 kill switch. Defined OUTSIDE the ENABLE_WORLD_STORAGE guard so it exists in both the
// real and the no-SQLite stub builds -- placing it beside a saveChunk definition put it inside
// the stub-only branch, and the real build failed to link.
namespace Phyxel { bool WorldStorage::s_lodPyramidOnSave = true; }

#ifndef ENABLE_WORLD_STORAGE
// Provide stub implementations when SQLite is not available
namespace Phyxel {

WorldStorage::WorldStorage(const std::string& databasePath) : dbPath(databasePath) {
    LOG_INFO("WorldStorage", "[WORLD_STORAGE] SQLite not available - using stub implementation");
}

WorldStorage::~WorldStorage() {
    close();
}

bool WorldStorage::initialize() {
    LOG_INFO("WorldStorage", "[WORLD_STORAGE] SQLite not available - world persistence disabled");
    return false; // Indicates no persistence available
}

bool WorldStorage::close() { return true; }
bool WorldStorage::saveChunk(const Chunk& chunk) { return false; }
bool WorldStorage::loadChunk(const glm::ivec3& chunkCoord, Chunk& chunk) { return false; }
bool WorldStorage::chunkExists(const glm::ivec3& chunkCoord) { return false; }
bool WorldStorage::deleteChunk(const glm::ivec3& chunkCoord) { return false; }
bool WorldStorage::deleteCube(const glm::ivec3& chunkCoord, const glm::ivec3& localPos) { return false; }
bool WorldStorage::saveLodBlob(const glm::ivec3&, int, const std::vector<uint8_t>&) { return false; }
bool WorldStorage::loadLodBlob(const glm::ivec3&, int, std::vector<uint8_t>&) { return false; }
bool WorldStorage::deleteLodBlobs(const glm::ivec3&) { return false; }
std::vector<int> WorldStorage::getLodLevels(const glm::ivec3&) { return {}; }
std::vector<glm::ivec3> WorldStorage::getChunksWithLodBlobs() { return {}; }
bool WorldStorage::saveChunks(const std::vector<std::reference_wrapper<const Chunk>>& chunks) { return false; }
bool WorldStorage::saveDirtyChunks(const std::vector<std::reference_wrapper<Chunk>>& chunks) { return false; }
std::vector<glm::ivec3> WorldStorage::getChunksInRegion(const glm::ivec3& minChunk, const glm::ivec3& maxChunk) { return {}; }
std::vector<glm::ivec3> WorldStorage::getAllChunkCoordinates() { return {}; }
bool WorldStorage::createNewWorld() { return false; }
bool WorldStorage::compactDatabase() { return false; }
size_t WorldStorage::getChunkCount() { return 0; }
size_t WorldStorage::getTotalCubeCount() { return 0; }
size_t WorldStorage::getDatabaseSize() { return 0; }
bool WorldStorage::createTables() { return false; }
bool WorldStorage::prepareStatements() { return false; }
void WorldStorage::finalizeStatements() {}
bool WorldStorage::beginTransaction() { return false; }
bool WorldStorage::commitTransaction() { return false; }
bool WorldStorage::rollbackTransaction() { return false; }
bool WorldStorage::loadSubcubesForChunk(const glm::ivec3& chunkCoord, Chunk& chunk) { return false; }
bool WorldStorage::loadMicrocubesForChunk(const glm::ivec3& chunkCoord, Chunk& chunk) { return false; }
bool WorldStorage::applyPragmas() { return false; }
bool WorldStorage::loadChunkFromBlob(const glm::ivec3& chunkCoord, Chunk& chunk, bool& found) { found = false; return false; }
bool WorldStorage::loadChunkFromLegacyRows(const glm::ivec3& chunkCoord, Chunk& chunk) { return false; }
bool WorldStorage::deleteLegacyRows(const glm::ivec3& chunkCoord) { return false; }
bool WorldStorage::migrateLegacyChunks() { return false; }
bool WorldStorage::setMeta(const std::string& key, const std::string& value) { return false; }
std::string WorldStorage::getMeta(const std::string& key) const { return ""; }
bool WorldStorage::hasMeta(const std::string& key) const { return false; }

} // namespace Phyxel

#else // HAVE_SQLITE3 is defined

#include <sqlite3.h>

namespace Phyxel {

WorldStorage::WorldStorage(const std::string& databasePath) : dbPath(databasePath) {
}

WorldStorage::~WorldStorage() {
    close();
}

bool WorldStorage::initialize() {
    // Ensure directory exists (skip for in-memory databases)
    if (dbPath != ":memory:") {
        std::filesystem::path dbPathObj(dbPath);
        std::filesystem::create_directories(dbPathObj.parent_path());
    }
    
    // Open database
    int result = sqlite3_open(dbPath.c_str(), &db);
    if (result != SQLITE_OK) {
        LOG_ERROR_FMT("WorldStorage", "[WORLD_STORAGE] Cannot open database: " << sqlite3_errmsg(db));
        return false;
    }
    
    LOG_INFO_FMT("WorldStorage", "[WORLD_STORAGE] Opened database: " << dbPath);

    // Tune the connection BEFORE any writes (WAL removes the fsync-per-commit
    // stall; page_size only takes effect on brand-new databases).
    applyPragmas();

    // Create tables if they don't exist
    if (!createTables()) {
        LOG_ERROR("WorldStorage", "[WORLD_STORAGE] Failed to create tables");
        return false;
    }

    // Prepare statements
    if (!prepareStatements()) {
        LOG_ERROR("WorldStorage", "[WORLD_STORAGE] Failed to prepare statements");
        return false;
    }

    // One-time storage v1 → v2 migration (row-per-voxel → palette+RLE blobs)
    if (!migrateLegacyChunks()) {
        LOG_ERROR("WorldStorage", "[WORLD_STORAGE] Legacy chunk migration failed — v1 rows left in place (loads fall back)");
    }

    LOG_INFO("WorldStorage", "[WORLD_STORAGE] WorldStorage initialized successfully");
    return true;
}

bool WorldStorage::applyPragmas() {
    if (!db) return false;
    // page_size must be set before the first write to matter (new DBs only).
    // WAL + synchronous=NORMAL: one fsync per checkpoint instead of per
    // commit, and readers never block the writer (the streaming gen worker
    // loads while the main thread saves, serialized by the caller's mutex).
    const char* pragmas =
        "PRAGMA page_size=8192;"
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "PRAGMA cache_size=-16384;"   // 16 MB page cache
        "PRAGMA mmap_size=268435456;" // 256 MB memory-mapped I/O
        "PRAGMA temp_store=MEMORY;"
        "PRAGMA journal_size_limit=67108864;"; // cap the WAL at 64 MB after checkpoints
    char* errorMsg = nullptr;
    bool success = sqlite3_exec(db, pragmas, nullptr, nullptr, &errorMsg) == SQLITE_OK;
    if (errorMsg) {
        LOG_WARN_FMT("WorldStorage", "[WORLD_STORAGE] PRAGMA setup warning: " << errorMsg);
        sqlite3_free(errorMsg);
    }
    return success;
}

bool WorldStorage::close() {
    finalizeStatements();
    
    if (db) {
        sqlite3_close(db);
        db = nullptr;
        LOG_INFO("WorldStorage", "[WORLD_STORAGE] Database closed");
    }
    return true;
}

bool WorldStorage::createTables() {
    if (!db) return false;
    
    const char* createTablesSQL = R"(
        -- Chunks table with metadata
        CREATE TABLE IF NOT EXISTS chunks (
            chunk_x INTEGER NOT NULL,
            chunk_y INTEGER NOT NULL,
            chunk_z INTEGER NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            modified_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            PRIMARY KEY (chunk_x, chunk_y, chunk_z)
        );
        
        -- Cubes table for sparse storage (only non-air blocks)
        CREATE TABLE IF NOT EXISTS cubes (
            chunk_x INTEGER NOT NULL,
            chunk_y INTEGER NOT NULL, 
            chunk_z INTEGER NOT NULL,
            local_x INTEGER NOT NULL CHECK(local_x >= 0 AND local_x < 32),
            local_y INTEGER NOT NULL CHECK(local_y >= 0 AND local_y < 32),
            local_z INTEGER NOT NULL CHECK(local_z >= 0 AND local_z < 32),
            is_subdivided INTEGER DEFAULT 0,
            is_visible INTEGER DEFAULT 1,
            material TEXT DEFAULT 'Default',
            PRIMARY KEY (chunk_x, chunk_y, chunk_z, local_x, local_y, local_z),
            FOREIGN KEY (chunk_x, chunk_y, chunk_z) REFERENCES chunks(chunk_x, chunk_y, chunk_z)
        );
        
        -- Subcubes table for subdivided cubes
        CREATE TABLE IF NOT EXISTS subcubes (
            chunk_x INTEGER NOT NULL,
            chunk_y INTEGER NOT NULL,
            chunk_z INTEGER NOT NULL,
            local_x INTEGER NOT NULL,
            local_y INTEGER NOT NULL,
            local_z INTEGER NOT NULL,
            sub_x INTEGER NOT NULL CHECK(sub_x >= 0 AND sub_x < 3),
            sub_y INTEGER NOT NULL CHECK(sub_y >= 0 AND sub_y < 3),
            sub_z INTEGER NOT NULL CHECK(sub_z >= 0 AND sub_z < 3),
            is_dynamic INTEGER DEFAULT 0,
            material TEXT DEFAULT 'Default',
            PRIMARY KEY (chunk_x, chunk_y, chunk_z, local_x, local_y, local_z, sub_x, sub_y, sub_z),
            FOREIGN KEY (chunk_x, chunk_y, chunk_z, local_x, local_y, local_z) 
                REFERENCES cubes(chunk_x, chunk_y, chunk_z, local_x, local_y, local_z)
        );
        
        -- Microcubes table for subdivided subcubes
        CREATE TABLE IF NOT EXISTS microcubes (
            chunk_x INTEGER NOT NULL,
            chunk_y INTEGER NOT NULL,
            chunk_z INTEGER NOT NULL,
            local_x INTEGER NOT NULL,
            local_y INTEGER NOT NULL,
            local_z INTEGER NOT NULL,
            sub_x INTEGER NOT NULL CHECK(sub_x >= 0 AND sub_x < 3),
            sub_y INTEGER NOT NULL CHECK(sub_y >= 0 AND sub_y < 3),
            sub_z INTEGER NOT NULL CHECK(sub_z >= 0 AND sub_z < 3),
            micro_x INTEGER NOT NULL CHECK(micro_x >= 0 AND micro_x < 3),
            micro_y INTEGER NOT NULL CHECK(micro_y >= 0 AND micro_y < 3),
            micro_z INTEGER NOT NULL CHECK(micro_z >= 0 AND micro_z < 3),
            material TEXT DEFAULT 'Default',
            PRIMARY KEY (chunk_x, chunk_y, chunk_z, local_x, local_y, local_z, sub_x, sub_y, sub_z, micro_x, micro_y, micro_z),
            FOREIGN KEY (chunk_x, chunk_y, chunk_z, local_x, local_y, local_z) 
                REFERENCES cubes(chunk_x, chunk_y, chunk_z, local_x, local_y, local_z)
        );
        
        -- Per-world metadata / generation recipe (key/value; value is text or JSON blob).
        -- Makes a world self-contained: seed, biome layout, extremeness, flora config.
        CREATE TABLE IF NOT EXISTS world_meta (
            key TEXT PRIMARY KEY,
            value TEXT
        );

        -- Storage v2: one palette+RLE blob per chunk (ChunkBlobCodec).
        -- Replaces the row-per-voxel tables above, which remain only as the
        -- legacy read/migration source. Counts are denormalized for stats.
        CREATE TABLE IF NOT EXISTS chunk_blobs (
            chunk_x INTEGER NOT NULL,
            chunk_y INTEGER NOT NULL,
            chunk_z INTEGER NOT NULL,
            version INTEGER NOT NULL,
            cube_count INTEGER NOT NULL DEFAULT 0,
            subcube_count INTEGER NOT NULL DEFAULT 0,
            microcube_count INTEGER NOT NULL DEFAULT 0,
            data BLOB NOT NULL,
            PRIMARY KEY (chunk_x, chunk_y, chunk_z)
        );

        -- C3 (docs/ContinuousLodPlan.md): the persisted LOD pyramid, keyed (x,y,z,lod) per
        -- godot_voxel's schema. Its own table and its own lifetime: written rarely (on chunk
        -- save), read often (whenever a distant region needs geometry without its
        -- full-resolution chunk becoming resident). One row per LEVEL, so a reader can fetch
        -- exactly the coarseness it needs instead of the whole pyramid.
        CREATE TABLE IF NOT EXISTS chunk_lod_blobs (
            chunk_x INTEGER NOT NULL,
            chunk_y INTEGER NOT NULL,
            chunk_z INTEGER NOT NULL,
            lod     INTEGER NOT NULL,
            version INTEGER NOT NULL,
            data    BLOB NOT NULL,
            PRIMARY KEY (chunk_x, chunk_y, chunk_z, lod)
        );

        -- The old secondary indexes duplicated each table's PRIMARY KEY
        -- prefix (pure write cost + file size) — drop them.
        DROP INDEX IF EXISTS idx_chunks_coord;
        DROP INDEX IF EXISTS idx_cubes_chunk;
        DROP INDEX IF EXISTS idx_subcubes_chunk;
        DROP INDEX IF EXISTS idx_microcubes_chunk;
    )";
    
    char* errorMsg = nullptr;
    bool success = sqlite3_exec(db, createTablesSQL, nullptr, nullptr, &errorMsg) == SQLITE_OK;
    
    if (errorMsg) {
        LOG_ERROR_FMT("WorldStorage", "[WORLD_STORAGE] Error creating tables: " << errorMsg);
        sqlite3_free(errorMsg);
    }
    
    // Migrate existing databases: add material column if missing
    if (success) {
        const char* migrationSQL = "ALTER TABLE cubes ADD COLUMN material TEXT DEFAULT 'Default';";
        char* migErr = nullptr;
        sqlite3_exec(db, migrationSQL, nullptr, nullptr, &migErr);
        if (migErr) {
            // "duplicate column name" is expected if column already exists — not an error
            std::string errStr(migErr);
            if (errStr.find("duplicate column") == std::string::npos) {
                LOG_WARN_FMT("WorldStorage", "[WORLD_STORAGE] Migration warning: " << migErr);
            }
            sqlite3_free(migErr);
        } else {
            LOG_INFO("WorldStorage", "[WORLD_STORAGE] Migrated cubes table: added material column");
        }

        // Migrate subcubes table
        char* subErr = nullptr;
        sqlite3_exec(db, "ALTER TABLE subcubes ADD COLUMN material TEXT DEFAULT 'Default';", nullptr, nullptr, &subErr);
        if (subErr) {
            std::string errStr(subErr);
            if (errStr.find("duplicate column") == std::string::npos)
                LOG_WARN_FMT("WorldStorage", "[WORLD_STORAGE] Migration warning (subcubes): " << subErr);
            sqlite3_free(subErr);
        }

        // Migrate microcubes table
        char* microErr = nullptr;
        sqlite3_exec(db, "ALTER TABLE microcubes ADD COLUMN material TEXT DEFAULT 'Default';", nullptr, nullptr, &microErr);
        if (microErr) {
            std::string errStr(microErr);
            if (errStr.find("duplicate column") == std::string::npos)
                LOG_WARN_FMT("WorldStorage", "[WORLD_STORAGE] Migration warning (microcubes): " << microErr);
            sqlite3_free(microErr);
        }
    }
    
    return success;
}

bool WorldStorage::prepareStatements() {
    // Insert statements
    const char* insertChunkSQL = R"(
        INSERT OR REPLACE INTO chunks (chunk_x, chunk_y, chunk_z, modified_at) 
        VALUES (?, ?, ?, CURRENT_TIMESTAMP);
    )";
    
    const char* insertCubeSQL = R"(
        INSERT OR REPLACE INTO cubes 
        (chunk_x, chunk_y, chunk_z, local_x, local_y, local_z, is_subdivided, is_visible, material) 
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    
    const char* insertSubcubeSQL = R"(
        INSERT OR REPLACE INTO subcubes 
        (chunk_x, chunk_y, chunk_z, local_x, local_y, local_z, sub_x, sub_y, sub_z, is_dynamic, material) 
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    
    const char* insertMicrocubeSQL = R"(
        INSERT OR REPLACE INTO microcubes 
        (chunk_x, chunk_y, chunk_z, local_x, local_y, local_z, sub_x, sub_y, sub_z, micro_x, micro_y, micro_z, material) 
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    
    // Select statements  
    const char* selectChunkSQL = R"(
        SELECT 1 FROM chunks WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?;
    )";
    
    const char* selectCubesSQL = R"(
        SELECT local_x, local_y, local_z, is_subdivided, is_visible, material 
        FROM cubes WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?;
    )";
    
    const char* selectSubcubesSQL = R"(
        SELECT local_x, local_y, local_z, sub_x, sub_y, sub_z, is_dynamic, material 
        FROM subcubes WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?;
    )";
    
    const char* selectMicrocubesSQL = R"(
        SELECT local_x, local_y, local_z, sub_x, sub_y, sub_z, micro_x, micro_y, micro_z, material 
        FROM microcubes WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?;
    )";
    
    const char* deleteChunkSQL = R"(
        DELETE FROM chunks WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?;
    )";
    
    const char* deleteCubeSQL = R"(
        DELETE FROM cubes WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ? AND local_x = ? AND local_y = ? AND local_z = ?;
    )";

    // Storage v2 blob statements + cached legacy-row deletes (the old code
    // re-prepared the per-chunk DELETEs on every save).
    const char* insertBlobSQL = R"(
        INSERT OR REPLACE INTO chunk_blobs
        (chunk_x, chunk_y, chunk_z, version, cube_count, subcube_count, microcube_count, data)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?);
    )";
    const char* selectBlobSQL = R"(
        SELECT data FROM chunk_blobs WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?;
    )";
    const char* deleteBlobSQL = R"(
        DELETE FROM chunk_blobs WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?;
    )";
    const char* insertLodBlobSQL = R"(
        INSERT OR REPLACE INTO chunk_lod_blobs
        (chunk_x, chunk_y, chunk_z, lod, version, data) VALUES (?, ?, ?, ?, ?, ?);
    )";
    const char* selectLodBlobSQL = R"(
        SELECT data FROM chunk_lod_blobs
        WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ? AND lod = ?;
    )";
    const char* deleteLodBlobSQL =
        "DELETE FROM chunk_lod_blobs WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?;";
    const char* deleteCubeRowsSQL = "DELETE FROM cubes WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?;";
    const char* deleteSubcubeRowsSQL = "DELETE FROM subcubes WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?;";
    const char* deleteMicrocubeRowsSQL = "DELETE FROM microcubes WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?;";

    // Prepare all statements
    return (sqlite3_prepare_v2(db, insertChunkSQL, -1, &insertChunkStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, insertCubeSQL, -1, &insertCubeStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, insertSubcubeSQL, -1, &insertSubcubeStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, insertMicrocubeSQL, -1, &insertMicrocubeStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, selectChunkSQL, -1, &selectChunkStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, selectCubesSQL, -1, &selectCubesStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, selectSubcubesSQL, -1, &selectSubcubesStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, selectMicrocubesSQL, -1, &selectMicrocubesStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, deleteChunkSQL, -1, &deleteChunkStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, deleteCubeSQL, -1, &deleteCubeStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, insertBlobSQL, -1, &insertBlobStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, selectBlobSQL, -1, &selectBlobStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, deleteBlobSQL, -1, &deleteBlobStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, insertLodBlobSQL, -1, &insertLodBlobStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, selectLodBlobSQL, -1, &selectLodBlobStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, deleteLodBlobSQL, -1, &deleteLodBlobStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, deleteCubeRowsSQL, -1, &deleteCubeRowsStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, deleteSubcubeRowsSQL, -1, &deleteSubcubeRowsStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, deleteMicrocubeRowsSQL, -1, &deleteMicrocubeRowsStmt, nullptr) == SQLITE_OK);
}

void WorldStorage::finalizeStatements() {
    if (insertChunkStmt) { sqlite3_finalize(insertChunkStmt); insertChunkStmt = nullptr; }
    if (insertCubeStmt) { sqlite3_finalize(insertCubeStmt); insertCubeStmt = nullptr; }
    if (insertSubcubeStmt) { sqlite3_finalize(insertSubcubeStmt); insertSubcubeStmt = nullptr; }
    if (insertMicrocubeStmt) { sqlite3_finalize(insertMicrocubeStmt); insertMicrocubeStmt = nullptr; }
    if (selectChunkStmt) { sqlite3_finalize(selectChunkStmt); selectChunkStmt = nullptr; }
    if (selectCubesStmt) { sqlite3_finalize(selectCubesStmt); selectCubesStmt = nullptr; }
    if (selectSubcubesStmt) { sqlite3_finalize(selectSubcubesStmt); selectSubcubesStmt = nullptr; }
    if (selectMicrocubesStmt) { sqlite3_finalize(selectMicrocubesStmt); selectMicrocubesStmt = nullptr; }
    if (deleteChunkStmt) { sqlite3_finalize(deleteChunkStmt); deleteChunkStmt = nullptr; }
    if (deleteCubeStmt) { sqlite3_finalize(deleteCubeStmt); deleteCubeStmt = nullptr; }
    if (insertBlobStmt) { sqlite3_finalize(insertBlobStmt); insertBlobStmt = nullptr; }
    if (selectBlobStmt) { sqlite3_finalize(selectBlobStmt); selectBlobStmt = nullptr; }
    if (deleteBlobStmt) { sqlite3_finalize(deleteBlobStmt); deleteBlobStmt = nullptr; }
    if (insertLodBlobStmt) { sqlite3_finalize(insertLodBlobStmt); insertLodBlobStmt = nullptr; }
    if (selectLodBlobStmt) { sqlite3_finalize(selectLodBlobStmt); selectLodBlobStmt = nullptr; }
    if (deleteLodBlobStmt) { sqlite3_finalize(deleteLodBlobStmt); deleteLodBlobStmt = nullptr; }
    if (deleteCubeRowsStmt) { sqlite3_finalize(deleteCubeRowsStmt); deleteCubeRowsStmt = nullptr; }
    if (deleteSubcubeRowsStmt) { sqlite3_finalize(deleteSubcubeRowsStmt); deleteSubcubeRowsStmt = nullptr; }
    if (deleteMicrocubeRowsStmt) { sqlite3_finalize(deleteMicrocubeRowsStmt); deleteMicrocubeRowsStmt = nullptr; }
}

bool WorldStorage::saveChunk(const Chunk& chunk) {
    return saveChunk(chunk, true); // Use transaction by default for standalone saves
}

bool WorldStorage::saveChunk(const Chunk& chunk, bool useTransaction) {
    if (!db) {
        LOG_ERROR("WorldStorage", "[WORLD_STORAGE] ERROR: No database connection in saveChunk()");
        return false;
    }
    
    glm::ivec3 chunkCoord = chunk.getWorldOrigin() / 32; // Convert world origin to chunk coordinates
    LOG_DEBUG_FMT("WorldStorage", "[WORLD_STORAGE] Starting saveChunk for chunk (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << ")");
    
    bool ownTransaction = false;
    if (useTransaction) {
        if (!beginTransaction()) {
            LOG_ERROR("WorldStorage", "[WORLD_STORAGE] ERROR: Failed to begin transaction in saveChunk()");
            return false;
        }
        ownTransaction = true;
    }

    {
        // Storage v2: the whole chunk becomes one palette+RLE blob row
        // (ChunkBlobCodec) — replaces ~32k row inserts per solid chunk.
        ChunkBlobCodec::Counts counts;
        std::vector<uint8_t> blob = ChunkBlobCodec::encode(chunk, &counts);

        // Chunk record (modified_at + chunkExists support)
        sqlite3_bind_int(insertChunkStmt, 1, chunkCoord.x);
        sqlite3_bind_int(insertChunkStmt, 2, chunkCoord.y);
        sqlite3_bind_int(insertChunkStmt, 3, chunkCoord.z);
        if (sqlite3_step(insertChunkStmt) != SQLITE_DONE) {
            LOG_ERROR_FMT("WorldStorage", "[WORLD_STORAGE] ERROR: Failed to insert chunk record: " << sqlite3_errmsg(db));
            sqlite3_reset(insertChunkStmt);
            if (ownTransaction) rollbackTransaction();
            return false;
        }
        sqlite3_reset(insertChunkStmt);

        // Blob upsert
        sqlite3_bind_int(insertBlobStmt, 1, chunkCoord.x);
        sqlite3_bind_int(insertBlobStmt, 2, chunkCoord.y);
        sqlite3_bind_int(insertBlobStmt, 3, chunkCoord.z);
        sqlite3_bind_int(insertBlobStmt, 4, ChunkBlobCodec::kCodecVersion);
        sqlite3_bind_int(insertBlobStmt, 5, static_cast<int>(counts.cubes));
        sqlite3_bind_int(insertBlobStmt, 6, static_cast<int>(counts.subcubes));
        sqlite3_bind_int(insertBlobStmt, 7, static_cast<int>(counts.microcubes));
        sqlite3_bind_blob(insertBlobStmt, 8, blob.data(), static_cast<int>(blob.size()), SQLITE_STATIC);
        if (sqlite3_step(insertBlobStmt) != SQLITE_DONE) {
            LOG_ERROR_FMT("WorldStorage", "[WORLD_STORAGE] ERROR: Failed to insert chunk blob: " << sqlite3_errmsg(db));
            sqlite3_reset(insertBlobStmt);
            if (ownTransaction) rollbackTransaction();
            return false;
        }
        sqlite3_reset(insertBlobStmt);

        // Drop any legacy v1 rows for this chunk (no-op once migrated)
        deleteLegacyRows(chunkCoord);

        LOG_DEBUG_FMT("WorldStorage", "[WORLD_STORAGE] Chunk (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z
                  << ") - Saved blob: " << blob.size() << " bytes (" << counts.cubes << " cubes, "
                  << counts.subcubes << " subcubes, " << counts.microcubes << " microcubes)");

        // C3.2: keep the persisted LOD pyramid in step with the voxels, INSIDE the same
        // transaction. Outside it, a crash between the two writes would leave a pyramid
        // describing a chunk that no longer exists -- stale geometry at distance, which reads
        // as "the world does not update until I walk up to it" rather than as a torn write.
        // refreshPyramid drops the old levels first, so a chunk that stops warranting one
        // (its structure demolished) stops being served.
        if (s_lodPyramidOnSave) Core::LodPyramidService::refreshPyramid(chunk, *this);
    }

    if (ownTransaction) {
        bool commitResult = commitTransaction();
        if (!commitResult) {
            LOG_ERROR("WorldStorage", "[WORLD_STORAGE] ERROR: Failed to commit transaction in saveChunk()");
        } else {
            LOG_DEBUG_FMT("WorldStorage", "[WORLD_STORAGE] Successfully saved chunk (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << ")");
        }
        return commitResult;
    } else {
        LOG_DEBUG_FMT("WorldStorage", "[WORLD_STORAGE] Successfully saved chunk (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << ") (no transaction commit needed)");
        return true;
    }
}

bool WorldStorage::loadChunk(const glm::ivec3& chunkCoord, Chunk& chunk) {
    if (!db) return false;

    // Storage v2 blob first; legacy v1 rows only as fallback for unmigrated data.
    bool blobFound = false;
    bool loaded = loadChunkFromBlob(chunkCoord, chunk, blobFound);
    if (blobFound) return loaded;

    return loadChunkFromLegacyRows(chunkCoord, chunk);
}

bool WorldStorage::loadChunkFromBlob(const glm::ivec3& chunkCoord, Chunk& chunk, bool& found) {
    found = false;
    if (!db || !selectBlobStmt) return false;

    sqlite3_bind_int(selectBlobStmt, 1, chunkCoord.x);
    sqlite3_bind_int(selectBlobStmt, 2, chunkCoord.y);
    sqlite3_bind_int(selectBlobStmt, 3, chunkCoord.z);

    bool decoded = false;
    ChunkBlobCodec::Counts counts;
    if (sqlite3_step(selectBlobStmt) == SQLITE_ROW) {
        found = true;
        const void* data = sqlite3_column_blob(selectBlobStmt, 0);
        const int size = sqlite3_column_bytes(selectBlobStmt, 0);
        decoded = ChunkBlobCodec::decode(static_cast<const uint8_t*>(data),
                                         static_cast<size_t>(size), chunk, &counts);
        if (!decoded) {
            // Fail loudly rather than silently falling back / regenerating —
            // a corrupt blob means saved player edits are at risk.
            LOG_ERROR_FMT("WorldStorage", "[WORLD_STORAGE] CORRUPT chunk blob at ("
                      << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z
                      << "), " << size << " bytes — load failed");
        }
    }
    sqlite3_reset(selectBlobStmt);

    if (!found || !decoded) return false;

    LOG_DEBUG_FMT("WorldStorage", "[WORLD_STORAGE] Loaded blob chunk ("
              << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << "): "
              << counts.cubes << " cubes, " << counts.subcubes << " subcubes, "
              << counts.microcubes << " microcubes");

    // Match v1 semantics: "loaded" means the chunk actually contained voxels.
    return (counts.cubes + counts.subcubes + counts.microcubes) > 0;
}

bool WorldStorage::loadChunkFromLegacyRows(const glm::ivec3& chunkCoord, Chunk& chunk) {
    if (!db) return false;

    LOG_INFO_FMT("WorldStorage", "[WORLD_STORAGE] Attempting to load chunk ("
              << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << ")");

    // Bind chunk coordinates
    sqlite3_bind_int(selectCubesStmt, 1, chunkCoord.x);
    sqlite3_bind_int(selectCubesStmt, 2, chunkCoord.y);
    sqlite3_bind_int(selectCubesStmt, 3, chunkCoord.z);
    
    int loadedCubes = 0;
    
    // Load cubes
    while (sqlite3_step(selectCubesStmt) == SQLITE_ROW) {
        int x = sqlite3_column_int(selectCubesStmt, 0);
        int y = sqlite3_column_int(selectCubesStmt, 1);
        int z = sqlite3_column_int(selectCubesStmt, 2);
        
        bool isSubdivided = sqlite3_column_int(selectCubesStmt, 3) != 0;
        bool isVisible = sqlite3_column_int(selectCubesStmt, 4) != 0;
        
        // Read material name (column 5), default to "Default" if NULL
        const char* materialStr = reinterpret_cast<const char*>(sqlite3_column_text(selectCubesStmt, 5));
        std::string material = materialStr ? materialStr : "Default";
        
        // If the cube is subdivided, it's a placeholder for subcubes/microcubes.
        // We should NOT create a full cube object here. The subcubes will be loaded later.
        if (isSubdivided) {
            continue;
        }

        if (chunk.addCube(glm::ivec3(x, y, z), material)) {
            // If the cube is invisible (but not subdivided), hide it — a store write (4.2b;
            // the old getCubeAt+setVisible would materialize a Cube per hidden voxel).
            if (!isVisible) {
                chunk.setCubeVisible(glm::ivec3(x, y, z), false);
            }
            loadedCubes++;
        }
    }
    
    sqlite3_reset(selectCubesStmt);
    
    // Load subcubes for this chunk
    loadSubcubesForChunk(chunkCoord, chunk);
    
    // Load microcubes for this chunk
    loadMicrocubesForChunk(chunkCoord, chunk);
    
    LOG_INFO_FMT("WorldStorage", "[WORLD_STORAGE] Loaded " << loadedCubes << " cubes for chunk (" 
              << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << ")");
    
    return loadedCubes > 0 || chunk.getStaticSubcubeCount() > 0 || chunk.getStaticMicrocubeCount() > 0;
}

bool WorldStorage::deleteLegacyRows(const glm::ivec3& chunkCoord) {
    if (!db) return false;
    bool ok = true;
    for (sqlite3_stmt* stmt : {deleteCubeRowsStmt, deleteSubcubeRowsStmt, deleteMicrocubeRowsStmt}) {
        if (!stmt) { ok = false; continue; }
        sqlite3_bind_int(stmt, 1, chunkCoord.x);
        sqlite3_bind_int(stmt, 2, chunkCoord.y);
        sqlite3_bind_int(stmt, 3, chunkCoord.z);
        ok = (sqlite3_step(stmt) == SQLITE_DONE) && ok;
        sqlite3_reset(stmt);
    }
    return ok;
}

bool WorldStorage::migrateLegacyChunks() {
    if (!db) return false;

    // Collect every chunk that still has v1 row-format data.
    std::vector<glm::ivec3> legacy;
    {
        const char* sql =
            "SELECT chunk_x, chunk_y, chunk_z FROM cubes "
            "UNION SELECT chunk_x, chunk_y, chunk_z FROM subcubes "
            "UNION SELECT chunk_x, chunk_y, chunk_z FROM microcubes;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            legacy.emplace_back(sqlite3_column_int(stmt, 0),
                                sqlite3_column_int(stmt, 1),
                                sqlite3_column_int(stmt, 2));
        }
        sqlite3_finalize(stmt);
    }
    if (legacy.empty()) return true;

    // Rollback-safety: snapshot the pre-migration DB so an older engine
    // binary can still open the world.
    if (dbPath != ":memory:") {
        try {
            std::filesystem::copy_file(dbPath, dbPath + ".v1.bak",
                                       std::filesystem::copy_options::overwrite_existing);
            LOG_INFO_FMT("WorldStorage", "[WORLD_STORAGE] Pre-migration backup written: " << dbPath << ".v1.bak");
        } catch (const std::exception& e) {
            LOG_WARN_FMT("WorldStorage", "[WORLD_STORAGE] Could not write pre-migration backup: " << e.what());
        }
    }

    LOG_INFO_FMT("WorldStorage", "[WORLD_STORAGE] Migrating " << legacy.size()
              << " legacy row-format chunk(s) to blob format (one-time)...");

    if (!beginTransaction()) return false;
    size_t migrated = 0;
    for (const auto& coord : legacy) {
        // A blob already present wins (e.g. a save happened before migration).
        sqlite3_bind_int(selectBlobStmt, 1, coord.x);
        sqlite3_bind_int(selectBlobStmt, 2, coord.y);
        sqlite3_bind_int(selectBlobStmt, 3, coord.z);
        bool hasBlob = sqlite3_step(selectBlobStmt) == SQLITE_ROW;
        sqlite3_reset(selectBlobStmt);
        if (hasBlob) {
            deleteLegacyRows(coord);
            continue;
        }

        Chunk temp(coord * 32);
        temp.initializeForLoading();
        loadChunkFromLegacyRows(coord, temp);

        ChunkBlobCodec::Counts counts;
        std::vector<uint8_t> blob = ChunkBlobCodec::encode(temp, &counts);

        sqlite3_bind_int(insertBlobStmt, 1, coord.x);
        sqlite3_bind_int(insertBlobStmt, 2, coord.y);
        sqlite3_bind_int(insertBlobStmt, 3, coord.z);
        sqlite3_bind_int(insertBlobStmt, 4, ChunkBlobCodec::kCodecVersion);
        sqlite3_bind_int(insertBlobStmt, 5, static_cast<int>(counts.cubes));
        sqlite3_bind_int(insertBlobStmt, 6, static_cast<int>(counts.subcubes));
        sqlite3_bind_int(insertBlobStmt, 7, static_cast<int>(counts.microcubes));
        sqlite3_bind_blob(insertBlobStmt, 8, blob.data(), static_cast<int>(blob.size()), SQLITE_STATIC);
        if (sqlite3_step(insertBlobStmt) != SQLITE_DONE) {
            LOG_ERROR_FMT("WorldStorage", "[WORLD_STORAGE] Migration failed for chunk ("
                      << coord.x << "," << coord.y << "," << coord.z << "): " << sqlite3_errmsg(db));
            sqlite3_reset(insertBlobStmt);
            rollbackTransaction();
            return false;
        }
        sqlite3_reset(insertBlobStmt);
        deleteLegacyRows(coord);

        ++migrated;
        if (migrated % 100 == 0) {
            LOG_INFO_FMT("WorldStorage", "[WORLD_STORAGE] Migration progress: " << migrated
                      << "/" << legacy.size() << " chunks");
        }
    }
    if (!commitTransaction()) return false;

    // Reclaim the freed row pages, otherwise the file keeps its v1 size.
    size_t sizeBefore = getDatabaseSize();
    compactDatabase();
    // The VACUUM writes through the WAL — checkpoint + truncate it so the
    // on-disk footprint actually shrinks even if the process is later killed.
    sqlite3_exec(db, "PRAGMA wal_checkpoint(TRUNCATE);", nullptr, nullptr, nullptr);
    LOG_INFO_FMT("WorldStorage", "[WORLD_STORAGE] Migration complete: " << migrated
              << " chunk(s) converted to blob format; database " << sizeBefore
              << " -> " << getDatabaseSize() << " bytes after VACUUM");
    return true;
}

bool WorldStorage::chunkExists(const glm::ivec3& chunkCoord) {
    if (!db || !selectChunkStmt) return false;
    
    sqlite3_bind_int(selectChunkStmt, 1, chunkCoord.x);
    sqlite3_bind_int(selectChunkStmt, 2, chunkCoord.y);
    sqlite3_bind_int(selectChunkStmt, 3, chunkCoord.z);
    
    bool exists = sqlite3_step(selectChunkStmt) == SQLITE_ROW;
    sqlite3_reset(selectChunkStmt);
    
    return exists;
}

bool WorldStorage::deleteChunk(const glm::ivec3& chunkCoord) {
    if (!db || !deleteChunkStmt) return false;
    
    // Delete associated blob, microcubes, subcubes, and cubes before removing chunk record
    const char* sql[] = {
        "DELETE FROM chunk_blobs WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?",
        "DELETE FROM microcubes WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?",
        "DELETE FROM subcubes WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?",
        "DELETE FROM cubes WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?"
    };
    for (const char* q : sql) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, q, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, chunkCoord.x);
            sqlite3_bind_int(stmt, 2, chunkCoord.y);
            sqlite3_bind_int(stmt, 3, chunkCoord.z);
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
    }
    
    sqlite3_bind_int(deleteChunkStmt, 1, chunkCoord.x);
    sqlite3_bind_int(deleteChunkStmt, 2, chunkCoord.y);
    sqlite3_bind_int(deleteChunkStmt, 3, chunkCoord.z);
    
    bool success = sqlite3_step(deleteChunkStmt) == SQLITE_DONE;
    sqlite3_reset(deleteChunkStmt);
    
    return success;
}

// --- C3: persisted LOD pyramid -----------------------------------------------------------
// Own table, own lifetime. Written rarely (chunk save), read often (distant regions), which is
// why it is NOT folded into chunk_blobs: a reader that only wants level 3 must not have to pull
// the full-resolution chunk blob to get it -- that would defeat the entire purpose.

bool WorldStorage::saveLodBlob(const glm::ivec3& chunkCoord, int lod,
                               const std::vector<uint8_t>& data) {
    if (!db || !insertLodBlobStmt || data.empty() || lod < 0) return false;
    sqlite3_reset(insertLodBlobStmt);
    sqlite3_bind_int(insertLodBlobStmt, 1, chunkCoord.x);
    sqlite3_bind_int(insertLodBlobStmt, 2, chunkCoord.y);
    sqlite3_bind_int(insertLodBlobStmt, 3, chunkCoord.z);
    sqlite3_bind_int(insertLodBlobStmt, 4, lod);
    sqlite3_bind_int(insertLodBlobStmt, 5, static_cast<int>(Core::LodBlobCodec::kCodecVersion));
    // SQLITE_TRANSIENT: sqlite copies the bytes rather than borrowing the caller's buffer.
    // Bind and step happen in this same call, so SQLITE_STATIC would also survive today's
    // callers (the temporary from encode() lives to the end of the full expression). TRANSIENT
    // is the defensive choice: it stays correct if anyone later splits bind and step across
    // calls, which is exactly when a borrowed pointer becomes a use-after-free.
    sqlite3_bind_blob(insertLodBlobStmt, 6, data.data(), static_cast<int>(data.size()),
                      SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(insertLodBlobStmt) == SQLITE_DONE;
    sqlite3_reset(insertLodBlobStmt);
    return ok;
}

bool WorldStorage::loadLodBlob(const glm::ivec3& chunkCoord, int lod,
                               std::vector<uint8_t>& outData) {
    outData.clear();
    if (!db || !selectLodBlobStmt || lod < 0) return false;
    sqlite3_reset(selectLodBlobStmt);
    sqlite3_bind_int(selectLodBlobStmt, 1, chunkCoord.x);
    sqlite3_bind_int(selectLodBlobStmt, 2, chunkCoord.y);
    sqlite3_bind_int(selectLodBlobStmt, 3, chunkCoord.z);
    sqlite3_bind_int(selectLodBlobStmt, 4, lod);
    bool ok = false;
    if (sqlite3_step(selectLodBlobStmt) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(selectLodBlobStmt, 0);
        const int n = sqlite3_column_bytes(selectLodBlobStmt, 0);
        if (blob && n > 0) {
            const uint8_t* p = static_cast<const uint8_t*>(blob);
            outData.assign(p, p + n);
            ok = true;
        }
    }
    sqlite3_reset(selectLodBlobStmt);
    return ok;
}

bool WorldStorage::deleteLodBlobs(const glm::ivec3& chunkCoord) {
    if (!db || !deleteLodBlobStmt) return false;
    sqlite3_reset(deleteLodBlobStmt);
    sqlite3_bind_int(deleteLodBlobStmt, 1, chunkCoord.x);
    sqlite3_bind_int(deleteLodBlobStmt, 2, chunkCoord.y);
    sqlite3_bind_int(deleteLodBlobStmt, 3, chunkCoord.z);
    const bool ok = sqlite3_step(deleteLodBlobStmt) == SQLITE_DONE;
    sqlite3_reset(deleteLodBlobStmt);
    return ok;
}

std::vector<glm::ivec3> WorldStorage::getChunksWithLodBlobs() {
    std::vector<glm::ivec3> coords;
    if (!db) return coords;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT DISTINCT chunk_x, chunk_y, chunk_z FROM chunk_lod_blobs;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return coords;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        coords.emplace_back(sqlite3_column_int(stmt, 0),
                            sqlite3_column_int(stmt, 1),
                            sqlite3_column_int(stmt, 2));
    }
    sqlite3_finalize(stmt);
    return coords;
}

std::vector<int> WorldStorage::getLodLevels(const glm::ivec3& chunkCoord) {
    std::vector<int> levels;
    if (!db) return levels;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT lod FROM chunk_lod_blobs WHERE chunk_x = ? AND chunk_y = ? "
                      "AND chunk_z = ? ORDER BY lod ASC;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return levels;
    sqlite3_bind_int(stmt, 1, chunkCoord.x);
    sqlite3_bind_int(stmt, 2, chunkCoord.y);
    sqlite3_bind_int(stmt, 3, chunkCoord.z);
    while (sqlite3_step(stmt) == SQLITE_ROW) levels.push_back(sqlite3_column_int(stmt, 0));
    sqlite3_finalize(stmt);
    return levels;
}

bool WorldStorage::deleteCube(const glm::ivec3& chunkCoord, const glm::ivec3& localPos) {
    if (!db || !deleteCubeStmt) return false;
    
    // Bind parameters
    sqlite3_bind_int(deleteCubeStmt, 1, chunkCoord.x);
    sqlite3_bind_int(deleteCubeStmt, 2, chunkCoord.y);
    sqlite3_bind_int(deleteCubeStmt, 3, chunkCoord.z);
    sqlite3_bind_int(deleteCubeStmt, 4, localPos.x);
    sqlite3_bind_int(deleteCubeStmt, 5, localPos.y);
    sqlite3_bind_int(deleteCubeStmt, 6, localPos.z);
    
    bool success = sqlite3_step(deleteCubeStmt) == SQLITE_DONE;
    sqlite3_reset(deleteCubeStmt);
    
    if (!success) {
        LOG_ERROR_FMT("WorldStorage", "[WORLD_STORAGE] Failed to delete cube at (" 
                  << localPos.x << "," << localPos.y << "," << localPos.z 
                  << ") in chunk (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << "): " 
                  << sqlite3_errmsg(db));
    }
    
    return success;
}

bool WorldStorage::saveChunks(const std::vector<std::reference_wrapper<const Chunk>>& chunks) {
    if (!db) return false;
    
    beginTransaction();

    for (const auto& chunk : chunks) {
        // false: already inside this batch's transaction (a nested BEGIN
        // would fail and abort the whole batch)
        if (!saveChunk(chunk.get(), false)) {
            rollbackTransaction();
            return false;
        }
    }

    return commitTransaction();
}

bool WorldStorage::saveDirtyChunks(const std::vector<std::reference_wrapper<Chunk>>& chunks) {
    if (!db) return false;
    
    int savedCount = 0;
    int skippedCount = 0;
    
    LOG_TRACE_FMT("WorldStorage", "[WORLD_STORAGE] Processing " << chunks.size() << " chunks for smart save...");
    
    beginTransaction();
    
    for (auto& chunkRef : chunks) {
        Chunk& chunk = chunkRef.get();
        glm::ivec3 chunkCoord = chunk.getWorldOrigin() / 32;
        
        if (chunk.getIsDirty()) {
            LOG_TRACE_FMT("WorldStorage", "[WORLD_STORAGE] Saving DIRTY chunk (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << ")");
            if (saveChunk(chunk, false)) { // Don't use nested transaction
                chunk.markClean();
                savedCount++;
            }
        } else {
            skippedCount++;
        }
    }
    
    bool success = commitTransaction();
    
    if (success) {
        LOG_DEBUG_FMT("WorldStorage", "[WORLD_STORAGE] Smart save complete: " << savedCount << " dirty chunks saved, " 
                  << skippedCount << " clean chunks skipped");
    } else {
        LOG_ERROR("WorldStorage", "[WORLD_STORAGE] Smart save FAILED!");
    }
    
    return success;
}

std::vector<glm::ivec3> WorldStorage::getChunksInRegion(const glm::ivec3& minChunk, const glm::ivec3& maxChunk) {
    std::vector<glm::ivec3> chunks;
    if (!db) return chunks;
    
    const char* sql = R"(
        SELECT DISTINCT chunk_x, chunk_y, chunk_z FROM chunks 
        WHERE chunk_x BETWEEN ? AND ? AND chunk_y BETWEEN ? AND ? AND chunk_z BETWEEN ? AND ?
        ORDER BY chunk_x, chunk_y, chunk_z;
    )";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return chunks;
    }
    
    sqlite3_bind_int(stmt, 1, minChunk.x);
    sqlite3_bind_int(stmt, 2, maxChunk.x);
    sqlite3_bind_int(stmt, 3, minChunk.y);
    sqlite3_bind_int(stmt, 4, maxChunk.y);
    sqlite3_bind_int(stmt, 5, minChunk.z);
    sqlite3_bind_int(stmt, 6, maxChunk.z);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        chunks.emplace_back(
            sqlite3_column_int(stmt, 0),
            sqlite3_column_int(stmt, 1),
            sqlite3_column_int(stmt, 2)
        );
    }
    
    sqlite3_finalize(stmt);
    return chunks;
}

std::vector<glm::ivec3> WorldStorage::getAllChunkCoordinates() {
    std::vector<glm::ivec3> chunks;
    if (!db) return chunks;
    
    const char* sql = "SELECT chunk_x, chunk_y, chunk_z FROM chunks ORDER BY chunk_x, chunk_y, chunk_z;";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR_FMT("WorldStorage", "[WORLD_STORAGE] Failed to prepare getAllChunkCoordinates query: " << sqlite3_errmsg(db));
        return chunks;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        chunks.emplace_back(
            sqlite3_column_int(stmt, 0),
            sqlite3_column_int(stmt, 1),
            sqlite3_column_int(stmt, 2)
        );
    }
    
    sqlite3_finalize(stmt);
    LOG_INFO_FMT("WorldStorage", "[WORLD_STORAGE] Found " << chunks.size() << " chunks in database");
    return chunks;
}

size_t WorldStorage::getChunkCount() {
    if (!db) return 0;
    
    const char* sql = "SELECT COUNT(*) FROM chunks";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    
    size_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
    }
    
    sqlite3_finalize(stmt);
    return count;
}

size_t WorldStorage::getTotalCubeCount() {
    if (!db) return 0;

    // Blob chunks carry a denormalized count; legacy rows count directly.
    const char* sql = "SELECT (SELECT COALESCE(SUM(cube_count), 0) FROM chunk_blobs) + (SELECT COUNT(*) FROM cubes)";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    
    size_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
    }
    
    sqlite3_finalize(stmt);
    return count;
}

size_t WorldStorage::getDatabaseSize() {
    if (!db) return 0;
    
    try {
        return std::filesystem::file_size(dbPath);
    } catch (...) {
        return 0;
    }
}

bool WorldStorage::compactDatabase() {
    if (!db) return false;
    
    char* errorMsg = nullptr;
    bool success = sqlite3_exec(db, "VACUUM;", nullptr, nullptr, &errorMsg) == SQLITE_OK;
    
    if (errorMsg) {
        LOG_ERROR_FMT("WorldStorage", "[WORLD_STORAGE] Error compacting database: " << errorMsg);
        sqlite3_free(errorMsg);
    }
    
    return success;
}

bool WorldStorage::createNewWorld() {
    if (!db) return false;
    
    // Clear all existing data
    const char* clearTables = R"(
        DELETE FROM chunk_blobs;
        DELETE FROM microcubes;
        DELETE FROM subcubes;
        DELETE FROM cubes;
        DELETE FROM chunks;
        VACUUM;
    )";
    
    char* errorMsg = nullptr;
    bool success = sqlite3_exec(db, clearTables, nullptr, nullptr, &errorMsg) == SQLITE_OK;
    
    if (errorMsg) {
        LOG_ERROR_FMT("WorldStorage", "[WORLD_STORAGE] Error clearing world: " << errorMsg);
        sqlite3_free(errorMsg);
    }
    
    return success;
}

bool WorldStorage::beginTransaction() {
    if (!db) return false;
    return sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool WorldStorage::commitTransaction() {
    if (!db) return false;
    return sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool WorldStorage::rollbackTransaction() {
    if (!db) return false;
    return sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool WorldStorage::loadSubcubesForChunk(const glm::ivec3& chunkCoord, Chunk& chunk) {
    if (!db) return false;
    
    // Bind chunk coordinates
    sqlite3_bind_int(selectSubcubesStmt, 1, chunkCoord.x);
    sqlite3_bind_int(selectSubcubesStmt, 2, chunkCoord.y);
    sqlite3_bind_int(selectSubcubesStmt, 3, chunkCoord.z);
    
    int loadedSubcubes = 0;
    
    // Load subcubes
    while (sqlite3_step(selectSubcubesStmt) == SQLITE_ROW) {
        int x = sqlite3_column_int(selectSubcubesStmt, 0);
        int y = sqlite3_column_int(selectSubcubesStmt, 1);
        int z = sqlite3_column_int(selectSubcubesStmt, 2);
        int subX = sqlite3_column_int(selectSubcubesStmt, 3);
        int subY = sqlite3_column_int(selectSubcubesStmt, 4);
        int subZ = sqlite3_column_int(selectSubcubesStmt, 5);
        
        bool isDynamic = sqlite3_column_int(selectSubcubesStmt, 6) != 0;
        const char* matText = reinterpret_cast<const char*>(sqlite3_column_text(selectSubcubesStmt, 7));
        std::string material = matText ? matText : "Default";
        
        // Add subcube to chunk
        if (chunk.addSubcube(glm::ivec3(x, y, z), glm::ivec3(subX, subY, subZ), material)) {
            loadedSubcubes++;
        }
    }
    
    sqlite3_reset(selectSubcubesStmt);
    
    if (loadedSubcubes > 0) {
        LOG_DEBUG_FMT("WorldStorage", "[WORLD_STORAGE] Loaded " << loadedSubcubes << " subcubes for chunk (" 
                  << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << ")");
    }
    
    return true;
}

bool WorldStorage::loadMicrocubesForChunk(const glm::ivec3& chunkCoord, Chunk& chunk) {
    if (!db) return false;
    
    // Bind chunk coordinates
    sqlite3_bind_int(selectMicrocubesStmt, 1, chunkCoord.x);
    sqlite3_bind_int(selectMicrocubesStmt, 2, chunkCoord.y);
    sqlite3_bind_int(selectMicrocubesStmt, 3, chunkCoord.z);
    
    int loadedMicrocubes = 0;
    
    // Load microcubes
    while (sqlite3_step(selectMicrocubesStmt) == SQLITE_ROW) {
        int x = sqlite3_column_int(selectMicrocubesStmt, 0);
        int y = sqlite3_column_int(selectMicrocubesStmt, 1);
        int z = sqlite3_column_int(selectMicrocubesStmt, 2);
        int subX = sqlite3_column_int(selectMicrocubesStmt, 3);
        int subY = sqlite3_column_int(selectMicrocubesStmt, 4);
        int subZ = sqlite3_column_int(selectMicrocubesStmt, 5);
        int microX = sqlite3_column_int(selectMicrocubesStmt, 6);
        int microY = sqlite3_column_int(selectMicrocubesStmt, 7);
        int microZ = sqlite3_column_int(selectMicrocubesStmt, 8);
        const char* matText = reinterpret_cast<const char*>(sqlite3_column_text(selectMicrocubesStmt, 9));
        std::string material = matText ? matText : "Default";
        
        // Add microcube to chunk
        if (chunk.addMicrocube(glm::ivec3(x, y, z), glm::ivec3(subX, subY, subZ), 
                               glm::ivec3(microX, microY, microZ), material)) {
            loadedMicrocubes++;
        }
    }
    
    sqlite3_reset(selectMicrocubesStmt);
    
    if (loadedMicrocubes > 0) {
        LOG_DEBUG_FMT("WorldStorage", "[WORLD_STORAGE] Loaded " << loadedMicrocubes << " microcubes for chunk ("
                  << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << ")");
    }

    return true;
}

// ---- Per-world metadata / recipe key-value store ----
// Infrequent (load/save time), so we prepare ad-hoc rather than caching statements.

bool WorldStorage::setMeta(const std::string& key, const std::string& value) {
    if (!db) return false;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO world_meta(key,value) VALUES(?,?) "
                      "ON CONFLICT(key) DO UPDATE SET value=excluded.value;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::string WorldStorage::getMeta(const std::string& key) const {
    std::string out;
    if (!db) return out;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT value FROM world_meta WHERE key=?;", -1, &stmt, nullptr) != SQLITE_OK)
        return out;
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* txt = sqlite3_column_text(stmt, 0);
        if (txt) out = reinterpret_cast<const char*>(txt);
    }
    sqlite3_finalize(stmt);
    return out;
}

bool WorldStorage::hasMeta(const std::string& key) const {
    if (!db) return false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM world_meta WHERE key=?;", -1, &stmt, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

} // namespace Phyxel

#endif // HAVE_SQLITE3
