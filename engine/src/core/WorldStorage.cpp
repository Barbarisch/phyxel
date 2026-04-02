#include "core/WorldStorage.h"
#include "core/Chunk.h"
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
    // Ensure directory exists
    std::filesystem::path dbPathObj(dbPath);
    std::filesystem::create_directories(dbPathObj.parent_path());
    
    // Open database
    int result = sqlite3_open(dbPath.c_str(), &db);
    if (result != SQLITE_OK) {
        LOG_ERROR_FMT("WorldStorage", "[WORLD_STORAGE] Cannot open database: " << sqlite3_errmsg(db));
        return false;
    }
    
    LOG_INFO_FMT("WorldStorage", "[WORLD_STORAGE] Opened database: " << dbPath);
    
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
    
    LOG_INFO("WorldStorage", "[WORLD_STORAGE] WorldStorage initialized successfully");
    return true;
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
        
        -- Spatial indexing for fast chunk queries
        CREATE INDEX IF NOT EXISTS idx_chunks_coord ON chunks(chunk_x, chunk_y, chunk_z);
        CREATE INDEX IF NOT EXISTS idx_cubes_chunk ON cubes(chunk_x, chunk_y, chunk_z);
        CREATE INDEX IF NOT EXISTS idx_subcubes_chunk ON subcubes(chunk_x, chunk_y, chunk_z);
        CREATE INDEX IF NOT EXISTS idx_microcubes_chunk ON microcubes(chunk_x, chunk_y, chunk_z);
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
            sqlite3_prepare_v2(db, deleteCubeSQL, -1, &deleteCubeStmt, nullptr) == SQLITE_OK);
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
    
    try {
        // CRITICAL: For dirty chunks, delete existing data first to handle deletions properly
        const char* deleteCubesSQL = "DELETE FROM cubes WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?";
        const char* deleteSubcubesSQL = "DELETE FROM subcubes WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?";
        const char* deleteMicrocubesSQL = "DELETE FROM microcubes WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?";
        
        sqlite3_stmt* deleteCubesStmt = nullptr;
        sqlite3_stmt* deleteSubcubesStmt = nullptr;
        sqlite3_stmt* deleteMicrocubesStmt = nullptr;
        
        if (sqlite3_prepare_v2(db, deleteCubesSQL, -1, &deleteCubesStmt, nullptr) != SQLITE_OK ||
            sqlite3_prepare_v2(db, deleteSubcubesSQL, -1, &deleteSubcubesStmt, nullptr) != SQLITE_OK ||
            sqlite3_prepare_v2(db, deleteMicrocubesSQL, -1, &deleteMicrocubesStmt, nullptr) != SQLITE_OK) {
            LOG_ERROR_FMT("WorldStorage", "[WORLD_STORAGE] ERROR: Failed to prepare delete statements: " << sqlite3_errmsg(db));
            if (ownTransaction) rollbackTransaction();
            return false;
        }
        
        // Delete old cubes
        sqlite3_bind_int(deleteCubesStmt, 1, chunkCoord.x);
        sqlite3_bind_int(deleteCubesStmt, 2, chunkCoord.y);
        sqlite3_bind_int(deleteCubesStmt, 3, chunkCoord.z);
        int deleteCubesResult = sqlite3_step(deleteCubesStmt);
        int deletedCubes = sqlite3_changes(db);
        sqlite3_finalize(deleteCubesStmt);
        
        // Delete old subcubes
        sqlite3_bind_int(deleteSubcubesStmt, 1, chunkCoord.x);
        sqlite3_bind_int(deleteSubcubesStmt, 2, chunkCoord.y);
        sqlite3_bind_int(deleteSubcubesStmt, 3, chunkCoord.z);
        int deleteSubcubesResult = sqlite3_step(deleteSubcubesStmt);
        int deletedSubcubes = sqlite3_changes(db);
        sqlite3_finalize(deleteSubcubesStmt);
        
        // Delete old microcubes
        sqlite3_bind_int(deleteMicrocubesStmt, 1, chunkCoord.x);
        sqlite3_bind_int(deleteMicrocubesStmt, 2, chunkCoord.y);
        sqlite3_bind_int(deleteMicrocubesStmt, 3, chunkCoord.z);
        int deleteMicrocubesResult = sqlite3_step(deleteMicrocubesStmt);
        int deletedMicrocubes = sqlite3_changes(db);
        sqlite3_finalize(deleteMicrocubesStmt);
        
        LOG_DEBUG_FMT("WorldStorage", "[WORLD_STORAGE] Chunk (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z 
                  << ") - Deleted " << deletedCubes << " old cubes, " << deletedSubcubes << " old subcubes, " << deletedMicrocubes << " old microcubes");
        
        // Insert/update chunk record
        sqlite3_bind_int(insertChunkStmt, 1, chunkCoord.x);
        sqlite3_bind_int(insertChunkStmt, 2, chunkCoord.y);
        sqlite3_bind_int(insertChunkStmt, 3, chunkCoord.z);
        
        if (sqlite3_step(insertChunkStmt) != SQLITE_DONE) {
            LOG_ERROR_FMT("WorldStorage", "[WORLD_STORAGE] ERROR: Failed to insert chunk record: " << sqlite3_errmsg(db));
            if (ownTransaction) rollbackTransaction();
            return false;
        }
        sqlite3_reset(insertChunkStmt);
        
        // Track which cubes have been saved to avoid duplicates and ensure parent cubes exist
        std::unordered_set<glm::ivec3, IVec3Hash> savedCubeLocations;
        
        int savedCubes = 0;
        
        // Save each cube (only non-air blocks to maintain sparseness)
        for (int x = 0; x < 32; ++x) {
            for (int y = 0; y < 32; ++y) {
                for (int z = 0; z < 32; ++z) {
                    glm::ivec3 localPos(x, y, z);
                    const Cube* cube = chunk.getCubeAt(localPos);
                    if (cube && cube->isVisible()) {
                        // Bind cube data
                        sqlite3_bind_int(insertCubeStmt, 1, chunkCoord.x);
                        sqlite3_bind_int(insertCubeStmt, 2, chunkCoord.y);
                        sqlite3_bind_int(insertCubeStmt, 3, chunkCoord.z);
                        sqlite3_bind_int(insertCubeStmt, 4, x);
                        sqlite3_bind_int(insertCubeStmt, 5, y);
                        sqlite3_bind_int(insertCubeStmt, 6, z);
                        sqlite3_bind_int(insertCubeStmt, 7, 0); // isSubdivided - always false now (subcubes stored at chunk level)
                        sqlite3_bind_int(insertCubeStmt, 8, cube->isVisible() ? 1 : 0);
                        sqlite3_bind_text(insertCubeStmt, 9, cube->getMaterialName().c_str(), -1, SQLITE_TRANSIENT);
                        
                        if (sqlite3_step(insertCubeStmt) != SQLITE_DONE) {
                            LOG_ERROR_FMT("WorldStorage", "[WORLD_STORAGE] ERROR: Failed to insert cube at (" << x << "," << y << "," << z << "): " << sqlite3_errmsg(db));
                            if (ownTransaction) rollbackTransaction();
                            return false;
                        }
                        sqlite3_reset(insertCubeStmt);
                        savedCubes++;
                        savedCubeLocations.insert(localPos);
                    }
                }
            }
        }
        
        // Save static subcubes that are stored directly in the chunk
        const auto& staticSubcubes = chunk.getStaticSubcubes();
        int savedSubcubes = 0;
        for (const auto& subcube : staticSubcubes) {
            if (subcube && subcube->isVisible()) {
                // Calculate parent cube local position from subcube's world position
                glm::ivec3 parentWorldPos = subcube->getPosition();
                glm::ivec3 parentLocalPos = parentWorldPos - chunkCoord * 32;
                
                // Validate parent position is within chunk bounds
                if (parentLocalPos.x >= 0 && parentLocalPos.x < 32 &&
                    parentLocalPos.y >= 0 && parentLocalPos.y < 32 &&
                    parentLocalPos.z >= 0 && parentLocalPos.z < 32) {
                    
                    // CRITICAL FIX: Ensure parent cube exists in DB (foreign key constraint)
                    if (savedCubeLocations.find(parentLocalPos) == savedCubeLocations.end()) {
                        // Insert placeholder cube (invisible, subdivided)
                        sqlite3_bind_int(insertCubeStmt, 1, chunkCoord.x);
                        sqlite3_bind_int(insertCubeStmt, 2, chunkCoord.y);
                        sqlite3_bind_int(insertCubeStmt, 3, chunkCoord.z);
                        sqlite3_bind_int(insertCubeStmt, 4, parentLocalPos.x);
                        sqlite3_bind_int(insertCubeStmt, 5, parentLocalPos.y);
                        sqlite3_bind_int(insertCubeStmt, 6, parentLocalPos.z);
                        sqlite3_bind_int(insertCubeStmt, 7, 1); // isSubdivided = true
                        sqlite3_bind_int(insertCubeStmt, 8, 0); // isVisible = false
                        sqlite3_bind_text(insertCubeStmt, 9, "Default", -1, SQLITE_STATIC);
                        
                        if (sqlite3_step(insertCubeStmt) != SQLITE_DONE) {
                            LOG_ERROR_FMT("WorldStorage", "[WORLD_STORAGE] ERROR: Failed to insert placeholder cube: " << sqlite3_errmsg(db));
                            if (ownTransaction) rollbackTransaction();
                            return false;
                        }
                        sqlite3_reset(insertCubeStmt);
                        savedCubeLocations.insert(parentLocalPos);
                    }
                    
                    // Bind static subcube data to prepared statement
                    sqlite3_bind_int(insertSubcubeStmt, 1, chunkCoord.x);
                    sqlite3_bind_int(insertSubcubeStmt, 2, chunkCoord.y);
                    sqlite3_bind_int(insertSubcubeStmt, 3, chunkCoord.z);
                    sqlite3_bind_int(insertSubcubeStmt, 4, parentLocalPos.x);
                    sqlite3_bind_int(insertSubcubeStmt, 5, parentLocalPos.y);
                    sqlite3_bind_int(insertSubcubeStmt, 6, parentLocalPos.z);
                    sqlite3_bind_int(insertSubcubeStmt, 7, subcube->getLocalPosition().x);
                    sqlite3_bind_int(insertSubcubeStmt, 8, subcube->getLocalPosition().y);
                    sqlite3_bind_int(insertSubcubeStmt, 9, subcube->getLocalPosition().z);
                    sqlite3_bind_int(insertSubcubeStmt, 10, subcube->isDynamic() ? 1 : 0);
                    sqlite3_bind_text(insertSubcubeStmt, 11, subcube->getMaterialName().c_str(), -1, SQLITE_TRANSIENT);
                    
                    if (sqlite3_step(insertSubcubeStmt) != SQLITE_DONE) {
                        LOG_ERROR_FMT("WorldStorage", "[WORLD_STORAGE] ERROR: Failed to insert static subcube at (" 
                                  << parentLocalPos.x << "," << parentLocalPos.y << "," << parentLocalPos.z << ") sub(" 
                                  << subcube->getLocalPosition().x << "," 
                                  << subcube->getLocalPosition().y << "," 
                                  << subcube->getLocalPosition().z << "): " 
                                  << sqlite3_errmsg(db));
                        if (ownTransaction) rollbackTransaction();
                        return false;
                    }
                    sqlite3_reset(insertSubcubeStmt);
                    savedSubcubes++;
                }
            }
        }
        
        // Save static microcubes
        const auto& staticMicrocubes = chunk.getStaticMicrocubes();
        int savedMicrocubes = 0;
        for (const auto& microcube : staticMicrocubes) {
            if (microcube && microcube->isVisible()) {
                // Calculate parent cube local position from microcube's parent position
                glm::ivec3 parentWorldPos = microcube->getParentCubePosition();
                glm::ivec3 parentLocalPos = parentWorldPos - chunkCoord * 32;
                
                // Validate parent position is within chunk bounds
                if (parentLocalPos.x >= 0 && parentLocalPos.x < 32 &&
                    parentLocalPos.y >= 0 && parentLocalPos.y < 32 &&
                    parentLocalPos.z >= 0 && parentLocalPos.z < 32) {
                    
                    // CRITICAL FIX: Ensure parent cube exists in DB (foreign key constraint)
                    if (savedCubeLocations.find(parentLocalPos) == savedCubeLocations.end()) {
                        // Insert placeholder cube (invisible, subdivided)
                        sqlite3_bind_int(insertCubeStmt, 1, chunkCoord.x);
                        sqlite3_bind_int(insertCubeStmt, 2, chunkCoord.y);
                        sqlite3_bind_int(insertCubeStmt, 3, chunkCoord.z);
                        sqlite3_bind_int(insertCubeStmt, 4, parentLocalPos.x);
                        sqlite3_bind_int(insertCubeStmt, 5, parentLocalPos.y);
                        sqlite3_bind_int(insertCubeStmt, 6, parentLocalPos.z);
                        sqlite3_bind_int(insertCubeStmt, 7, 1); // isSubdivided = true
                        sqlite3_bind_int(insertCubeStmt, 8, 0); // isVisible = false
                        sqlite3_bind_text(insertCubeStmt, 9, "Default", -1, SQLITE_STATIC);
                        
                        if (sqlite3_step(insertCubeStmt) != SQLITE_DONE) {
                            LOG_ERROR_FMT("WorldStorage", "[WORLD_STORAGE] ERROR: Failed to insert placeholder cube for microcube: " << sqlite3_errmsg(db));
                            if (ownTransaction) rollbackTransaction();
                            return false;
                        }
                        sqlite3_reset(insertCubeStmt);
                        savedCubeLocations.insert(parentLocalPos);
                    }
                    
                    // Bind microcube data
                    sqlite3_bind_int(insertMicrocubeStmt, 1, chunkCoord.x);
                    sqlite3_bind_int(insertMicrocubeStmt, 2, chunkCoord.y);
                    sqlite3_bind_int(insertMicrocubeStmt, 3, chunkCoord.z);
                    sqlite3_bind_int(insertMicrocubeStmt, 4, parentLocalPos.x);
                    sqlite3_bind_int(insertMicrocubeStmt, 5, parentLocalPos.y);
                    sqlite3_bind_int(insertMicrocubeStmt, 6, parentLocalPos.z);
                    sqlite3_bind_int(insertMicrocubeStmt, 7, microcube->getSubcubeLocalPosition().x);
                    sqlite3_bind_int(insertMicrocubeStmt, 8, microcube->getSubcubeLocalPosition().y);
                    sqlite3_bind_int(insertMicrocubeStmt, 9, microcube->getSubcubeLocalPosition().z);
                    sqlite3_bind_int(insertMicrocubeStmt, 10, microcube->getMicrocubeLocalPosition().x);
                    sqlite3_bind_int(insertMicrocubeStmt, 11, microcube->getMicrocubeLocalPosition().y);
                    sqlite3_bind_int(insertMicrocubeStmt, 12, microcube->getMicrocubeLocalPosition().z);
                    sqlite3_bind_text(insertMicrocubeStmt, 13, microcube->getMaterialName().c_str(), -1, SQLITE_TRANSIENT);
                    
                    if (sqlite3_step(insertMicrocubeStmt) != SQLITE_DONE) {
                        LOG_ERROR_FMT("WorldStorage", "[WORLD_STORAGE] ERROR: Failed to insert microcube: " << sqlite3_errmsg(db));
                        if (ownTransaction) rollbackTransaction();
                        return false;
                    }
                    sqlite3_reset(insertMicrocubeStmt);
                    savedMicrocubes++;
                }
            }
        }
        
        LOG_DEBUG_FMT("WorldStorage", "[WORLD_STORAGE] Chunk (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z 
                  << ") - Saved " << savedCubes << " cubes, " << savedSubcubes << " subcubes, " << savedMicrocubes << " microcubes");
        
    } catch (...) {
        LOG_ERROR("WorldStorage", "[WORLD_STORAGE] ERROR: Exception caught in saveChunk()");
        if (ownTransaction) rollbackTransaction();
        return false;
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
            // If the cube is invisible (but not subdivided), hide it
            if (!isVisible) {
                Cube* cube = chunk.getCubeAt(glm::ivec3(x, y, z));
                if (cube) {
                    cube->setVisible(false);
                }
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
    
    // Delete associated microcubes, subcubes, and cubes before removing chunk record
    const char* sql[] = {
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
        if (!saveChunk(chunk.get())) {
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
    
    const char* sql = "SELECT COUNT(*) FROM cubes";
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

} // namespace Phyxel

#endif // HAVE_SQLITE3
