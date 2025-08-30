#include "core/WorldStorage.h"
#include "core/Chunk.h"
#include "core/Cube.h"
#include "core/Subcube.h"
#include <iostream>
#include <filesystem>

#ifdef ENABLE_WORLD_STORAGE
#include <sqlite3.h>
#endif

// Only compile SQLite functionality if available
#ifndef ENABLE_WORLD_STORAGE
// Provide stub implementations when SQLite is not available
namespace VulkanCube {

WorldStorage::WorldStorage(const std::string& databasePath) : dbPath(databasePath) {
    std::cout << "[WORLD_STORAGE] SQLite not available - using stub implementation" << std::endl;
}

WorldStorage::~WorldStorage() {
    close();
}

bool WorldStorage::initialize() {
    std::cout << "[WORLD_STORAGE] SQLite not available - world persistence disabled" << std::endl;
    return false; // Indicates no persistence available
}

bool WorldStorage::close() { return true; }
bool WorldStorage::saveChunk(const Chunk& chunk) { return false; }
bool WorldStorage::loadChunk(const glm::ivec3& chunkCoord, Chunk& chunk) { return false; }
bool WorldStorage::chunkExists(const glm::ivec3& chunkCoord) { return false; }
bool WorldStorage::deleteChunk(const glm::ivec3& chunkCoord) { return false; }
bool WorldStorage::saveChunks(const std::vector<std::reference_wrapper<const Chunk>>& chunks) { return false; }
std::vector<glm::ivec3> WorldStorage::getChunksInRegion(const glm::ivec3& minChunk, const glm::ivec3& maxChunk) { return {}; }
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

} // namespace VulkanCube

#else // HAVE_SQLITE3 is defined

#include <sqlite3.h>

namespace VulkanCube {

WorldStorage::WorldStorage(const std::string& databasePath) : dbPath(databasePath) {
}

WorldStorage::~WorldStorage() {
    close();
}

bool WorldStorage::initialize() {
    // Create directory if it doesn't exist
    std::filesystem::path dbFile(dbPath);
    std::filesystem::create_directories(dbFile.parent_path());
    
    // Open database
    int result = sqlite3_open(dbPath.c_str(), &db);
    if (result != SQLITE_OK) {
        std::cerr << "[WORLD_STORAGE] Failed to open database: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    // Enable foreign keys and optimize performance
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);  // Better concurrency
    sqlite3_exec(db, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr); // Balance safety/performance
    sqlite3_exec(db, "PRAGMA cache_size = 10000;", nullptr, nullptr, nullptr);   // 10MB cache
    
    // Create tables if they don't exist
    if (!createTables()) {
        std::cerr << "[WORLD_STORAGE] Failed to create tables" << std::endl;
        return false;
    }
    
    // Prepare statements for better performance
    if (!prepareStatements()) {
        std::cerr << "[WORLD_STORAGE] Failed to prepare statements" << std::endl;
        return false;
    }
    
    std::cout << "[WORLD_STORAGE] Database initialized: " << dbPath << std::endl;
    return true;
}

bool WorldStorage::close() {
    if (db) {
        finalizeStatements();
        sqlite3_close(db);
        db = nullptr;
    }
    return true;
}

bool WorldStorage::createTables() {
    const char* createChunksTable = R"(
        CREATE TABLE IF NOT EXISTS chunks (
            chunk_x INTEGER,
            chunk_y INTEGER,
            chunk_z INTEGER,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            modified_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            neighbor_hash TEXT DEFAULT NULL,  -- Hash of neighboring chunk states for cache invalidation
            face_count INTEGER DEFAULT 0,    -- Number of visible faces (for performance monitoring)
            PRIMARY KEY (chunk_x, chunk_y, chunk_z)
        );
    )";
    
    const char* createCubesTable = R"(
        CREATE TABLE IF NOT EXISTS cubes (
            chunk_x INTEGER,
            chunk_y INTEGER,
            chunk_z INTEGER,
            local_x INTEGER CHECK(local_x >= 0 AND local_x <= 31),
            local_y INTEGER CHECK(local_y >= 0 AND local_y <= 31),
            local_z INTEGER CHECK(local_z >= 0 AND local_z <= 31),
            color_r REAL CHECK(color_r >= 0.0 AND color_r <= 1.0),
            color_g REAL CHECK(color_g >= 0.0 AND color_g <= 1.0),
            color_b REAL CHECK(color_b >= 0.0 AND color_b <= 1.0),
            material_id INTEGER DEFAULT 1,
            is_subdivided BOOLEAN DEFAULT FALSE,
            is_visible BOOLEAN DEFAULT TRUE,
            FOREIGN KEY (chunk_x, chunk_y, chunk_z) REFERENCES chunks(chunk_x, chunk_y, chunk_z),
            PRIMARY KEY (chunk_x, chunk_y, chunk_z, local_x, local_y, local_z)
        );
    )";
    
    const char* createSubcubesTable = R"(
        CREATE TABLE IF NOT EXISTS subcubes (
            chunk_x INTEGER,
            chunk_y INTEGER,
            chunk_z INTEGER,
            local_x INTEGER,
            local_y INTEGER,
            local_z INTEGER,
            sub_x INTEGER CHECK(sub_x >= 0 AND sub_x <= 2),
            sub_y INTEGER CHECK(sub_y >= 0 AND sub_y <= 2),
            sub_z INTEGER CHECK(sub_z >= 0 AND sub_z <= 2),
            color_r REAL CHECK(color_r >= 0.0 AND color_r <= 1.0),
            color_g REAL CHECK(color_g >= 0.0 AND color_g <= 1.0),
            color_b REAL CHECK(color_b >= 0.0 AND color_b <= 1.0),
            is_dynamic BOOLEAN DEFAULT FALSE,
            FOREIGN KEY (chunk_x, chunk_y, chunk_z, local_x, local_y, local_z) 
                REFERENCES cubes(chunk_x, chunk_y, chunk_z, local_x, local_y, local_z),
            PRIMARY KEY (chunk_x, chunk_y, chunk_z, local_x, local_y, local_z, sub_x, sub_y, sub_z)
        );
    )";
    
    // Create indexes for spatial queries
    const char* createIndexes = R"(
        CREATE INDEX IF NOT EXISTS idx_chunks_spatial ON chunks(chunk_x, chunk_y, chunk_z);
        CREATE INDEX IF NOT EXISTS idx_cubes_spatial ON cubes(chunk_x, chunk_y, chunk_z);
        CREATE INDEX IF NOT EXISTS idx_cubes_local ON cubes(local_x, local_y, local_z);
        CREATE INDEX IF NOT EXISTS idx_subcubes_spatial ON subcubes(chunk_x, chunk_y, chunk_z, local_x, local_y, local_z);
    )";
    
    char* errorMsg = nullptr;
    
    // Execute table creation
    if (sqlite3_exec(db, createChunksTable, nullptr, nullptr, &errorMsg) != SQLITE_OK ||
        sqlite3_exec(db, createCubesTable, nullptr, nullptr, &errorMsg) != SQLITE_OK ||
        sqlite3_exec(db, createSubcubesTable, nullptr, nullptr, &errorMsg) != SQLITE_OK ||
        sqlite3_exec(db, createIndexes, nullptr, nullptr, &errorMsg) != SQLITE_OK) {
        
        if (errorMsg) {
            std::cerr << "[WORLD_STORAGE] SQL error: " << errorMsg << std::endl;
            sqlite3_free(errorMsg);
        }
        return false;
    }
    
    return true;
}

bool WorldStorage::prepareStatements() {
    // Insert statements
    const char* insertChunkSQL = R"(
        INSERT OR REPLACE INTO chunks (chunk_x, chunk_y, chunk_z, modified_at) 
        VALUES (?, ?, ?, CURRENT_TIMESTAMP);
    )";
    
    const char* insertCubeSQL = R"(
        INSERT OR REPLACE INTO cubes 
        (chunk_x, chunk_y, chunk_z, local_x, local_y, local_z, color_r, color_g, color_b, is_subdivided, is_visible) 
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    
    const char* insertSubcubeSQL = R"(
        INSERT OR REPLACE INTO subcubes 
        (chunk_x, chunk_y, chunk_z, local_x, local_y, local_z, sub_x, sub_y, sub_z, color_r, color_g, color_b, is_dynamic) 
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    
    // Select statements  
    const char* selectChunkSQL = R"(
        SELECT 1 FROM chunks WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?;
    )";
    
    const char* selectCubesSQL = R"(
        SELECT local_x, local_y, local_z, color_r, color_g, color_b, is_subdivided, is_visible 
        FROM cubes WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?;
    )";
    
    const char* selectSubcubesSQL = R"(
        SELECT local_x, local_y, local_z, sub_x, sub_y, sub_z, color_r, color_g, color_b, is_dynamic 
        FROM subcubes WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?;
    )";
    
    const char* deleteChunkSQL = R"(
        DELETE FROM chunks WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?;
    )";
    
    // Prepare all statements
    return (sqlite3_prepare_v2(db, insertChunkSQL, -1, &insertChunkStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, insertCubeSQL, -1, &insertCubeStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, insertSubcubeSQL, -1, &insertSubcubeStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, selectChunkSQL, -1, &selectChunkStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, selectCubesSQL, -1, &selectCubesStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, selectSubcubesSQL, -1, &selectSubcubesStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, deleteChunkSQL, -1, &deleteChunkStmt, nullptr) == SQLITE_OK);
}

void WorldStorage::finalizeStatements() {
    if (insertChunkStmt) { sqlite3_finalize(insertChunkStmt); insertChunkStmt = nullptr; }
    if (insertCubeStmt) { sqlite3_finalize(insertCubeStmt); insertCubeStmt = nullptr; }
    if (insertSubcubeStmt) { sqlite3_finalize(insertSubcubeStmt); insertSubcubeStmt = nullptr; }
    if (selectChunkStmt) { sqlite3_finalize(selectChunkStmt); selectChunkStmt = nullptr; }
    if (selectCubesStmt) { sqlite3_finalize(selectCubesStmt); selectCubesStmt = nullptr; }
    if (selectSubcubesStmt) { sqlite3_finalize(selectSubcubesStmt); selectSubcubesStmt = nullptr; }
    if (deleteChunkStmt) { sqlite3_finalize(deleteChunkStmt); deleteChunkStmt = nullptr; }
}

bool WorldStorage::saveChunk(const Chunk& chunk) {
    if (!db) return false;
    
    glm::ivec3 chunkCoord = chunk.getWorldOrigin() / 32; // Convert world origin to chunk coordinates
    
    if (!beginTransaction()) return false;
    
    try {
        // Insert/update chunk record
        sqlite3_bind_int(insertChunkStmt, 1, chunkCoord.x);
        sqlite3_bind_int(insertChunkStmt, 2, chunkCoord.y);
        sqlite3_bind_int(insertChunkStmt, 3, chunkCoord.z);
        
        if (sqlite3_step(insertChunkStmt) != SQLITE_DONE) {
            rollbackTransaction();
            return false;
        }
        sqlite3_reset(insertChunkStmt);
        
        // Save each cube (only non-air blocks to maintain sparseness)
        for (int x = 0; x < 32; ++x) {
            for (int y = 0; y < 32; ++y) {
                for (int z = 0; z < 32; ++z) {
                    const Cube* cube = chunk.getCubeAt(glm::ivec3(x, y, z));
                    if (cube && cube->isVisible()) {
                        // Bind cube data
                        sqlite3_bind_int(insertCubeStmt, 1, chunkCoord.x);
                        sqlite3_bind_int(insertCubeStmt, 2, chunkCoord.y);
                        sqlite3_bind_int(insertCubeStmt, 3, chunkCoord.z);
                        sqlite3_bind_int(insertCubeStmt, 4, x);
                        sqlite3_bind_int(insertCubeStmt, 5, y);
                        sqlite3_bind_int(insertCubeStmt, 6, z);
                        sqlite3_bind_double(insertCubeStmt, 7, cube->getColor().r);
                        sqlite3_bind_double(insertCubeStmt, 8, cube->getColor().g);
                        sqlite3_bind_double(insertCubeStmt, 9, cube->getColor().b);
                        sqlite3_bind_int(insertCubeStmt, 10, cube->isSubdivided() ? 1 : 0);
                        sqlite3_bind_int(insertCubeStmt, 11, cube->isVisible() ? 1 : 0);
                        
                        if (sqlite3_step(insertCubeStmt) != SQLITE_DONE) {
                            rollbackTransaction();
                            return false;
                        }
                        sqlite3_reset(insertCubeStmt);
                        
                        // Save subcubes if subdivided
                        if (cube->isSubdivided()) {
                            for (const Subcube* subcube : cube->getSubcubes()) {
                                if (subcube) {
                                    glm::ivec3 subPos = subcube->getLocalPosition();
                                    
                                    sqlite3_bind_int(insertSubcubeStmt, 1, chunkCoord.x);
                                    sqlite3_bind_int(insertSubcubeStmt, 2, chunkCoord.y);
                                    sqlite3_bind_int(insertSubcubeStmt, 3, chunkCoord.z);
                                    sqlite3_bind_int(insertSubcubeStmt, 4, x);
                                    sqlite3_bind_int(insertSubcubeStmt, 5, y);
                                    sqlite3_bind_int(insertSubcubeStmt, 6, z);
                                    sqlite3_bind_int(insertSubcubeStmt, 7, subPos.x);
                                    sqlite3_bind_int(insertSubcubeStmt, 8, subPos.y);
                                    sqlite3_bind_int(insertSubcubeStmt, 9, subPos.z);
                                    sqlite3_bind_double(insertSubcubeStmt, 10, subcube->getColor().r);
                                    sqlite3_bind_double(insertSubcubeStmt, 11, subcube->getColor().g);
                                    sqlite3_bind_double(insertSubcubeStmt, 12, subcube->getColor().b);
                                    sqlite3_bind_int(insertSubcubeStmt, 13, subcube->isDynamic() ? 1 : 0);
                                    
                                    if (sqlite3_step(insertSubcubeStmt) != SQLITE_DONE) {
                                        rollbackTransaction();
                                        return false;
                                    }
                                    sqlite3_reset(insertSubcubeStmt);
                                }
                            }
                        }
                    }
                }
            }
        }
        
        return commitTransaction();
    } catch (...) {
        rollbackTransaction();
        return false;
    }
}

bool WorldStorage::loadChunk(const glm::ivec3& chunkCoord, Chunk& chunk) {
    if (!db) return false;
    
    // Check if chunk exists
    sqlite3_bind_int(selectChunkStmt, 1, chunkCoord.x);
    sqlite3_bind_int(selectChunkStmt, 2, chunkCoord.y);
    sqlite3_bind_int(selectChunkStmt, 3, chunkCoord.z);
    
    bool chunkExists = (sqlite3_step(selectChunkStmt) == SQLITE_ROW);
    sqlite3_reset(selectChunkStmt);
    
    if (!chunkExists) {
        return false; // Chunk doesn't exist in database
    }
    
    // Clear existing chunk data
    // Note: You'll need to add a method to clear chunk data without destroying it
    // chunk.clear(); 
    
    // Load cubes
    sqlite3_bind_int(selectCubesStmt, 1, chunkCoord.x);
    sqlite3_bind_int(selectCubesStmt, 2, chunkCoord.y);
    sqlite3_bind_int(selectCubesStmt, 3, chunkCoord.z);
    
    int cubesLoaded = 0;
    while (sqlite3_step(selectCubesStmt) == SQLITE_ROW) {
        int localX = sqlite3_column_int(selectCubesStmt, 0);
        int localY = sqlite3_column_int(selectCubesStmt, 1);
        int localZ = sqlite3_column_int(selectCubesStmt, 2);
        float colorR = static_cast<float>(sqlite3_column_double(selectCubesStmt, 3));
        float colorG = static_cast<float>(sqlite3_column_double(selectCubesStmt, 4));
        float colorB = static_cast<float>(sqlite3_column_double(selectCubesStmt, 5));
        bool isSubdivided = sqlite3_column_int(selectCubesStmt, 6) != 0;
        bool isVisible = sqlite3_column_int(selectCubesStmt, 7) != 0;
        
        glm::ivec3 localPos(localX, localY, localZ);
        glm::vec3 color(colorR, colorG, colorB);
        
        // Add cube to chunk
        chunk.addCube(localPos, color);
        cubesLoaded++;
        
        Cube* cube = chunk.getCubeAt(localPos);
        if (cube) {
            cube->setVisible(isVisible);
            if (isSubdivided) {
                chunk.subdivideAt(localPos);
            }
        }
    }
    sqlite3_reset(selectCubesStmt);
    
    std::cout << "[WORLD_STORAGE] Loaded " << cubesLoaded << " cubes for chunk (" 
              << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << ")" << std::endl;
    
    // Load subcubes
    return loadSubcubesForChunk(chunkCoord, chunk);
}

bool WorldStorage::loadSubcubesForChunk(const glm::ivec3& chunkCoord, Chunk& chunk) {
    sqlite3_bind_int(selectSubcubesStmt, 1, chunkCoord.x);
    sqlite3_bind_int(selectSubcubesStmt, 2, chunkCoord.y);
    sqlite3_bind_int(selectSubcubesStmt, 3, chunkCoord.z);
    
    while (sqlite3_step(selectSubcubesStmt) == SQLITE_ROW) {
        int localX = sqlite3_column_int(selectSubcubesStmt, 0);
        int localY = sqlite3_column_int(selectSubcubesStmt, 1);
        int localZ = sqlite3_column_int(selectSubcubesStmt, 2);
        int subX = sqlite3_column_int(selectSubcubesStmt, 3);
        int subY = sqlite3_column_int(selectSubcubesStmt, 4);
        int subZ = sqlite3_column_int(selectSubcubesStmt, 5);
        float colorR = static_cast<float>(sqlite3_column_double(selectSubcubesStmt, 6));
        float colorG = static_cast<float>(sqlite3_column_double(selectSubcubesStmt, 7));
        float colorB = static_cast<float>(sqlite3_column_double(selectSubcubesStmt, 8));
        bool isDynamic = sqlite3_column_int(selectSubcubesStmt, 9) != 0;
        
        glm::ivec3 parentPos(localX, localY, localZ);
        glm::ivec3 subcubePos(subX, subY, subZ);
        glm::vec3 color(colorR, colorG, colorB);
        
        // Add subcube to chunk
        chunk.addSubcube(parentPos, subcubePos, color);
        
        // Note: Handle dynamic subcubes if needed
        // if (isDynamic) { /* transfer to global dynamic system */ }
    }
    sqlite3_reset(selectSubcubesStmt);
    
    return true;
}

bool WorldStorage::chunkExists(const glm::ivec3& chunkCoord) {
    if (!db) return false;
    
    sqlite3_bind_int(selectChunkStmt, 1, chunkCoord.x);
    sqlite3_bind_int(selectChunkStmt, 2, chunkCoord.y);
    sqlite3_bind_int(selectChunkStmt, 3, chunkCoord.z);
    
    bool exists = (sqlite3_step(selectChunkStmt) == SQLITE_ROW);
    sqlite3_reset(selectChunkStmt);
    
    return exists;
}

std::vector<glm::ivec3> WorldStorage::getChunksInRegion(const glm::ivec3& minChunk, const glm::ivec3& maxChunk) {
    std::vector<glm::ivec3> chunks;
    
    if (!db) return chunks;
    
    const char* query = R"(
        SELECT chunk_x, chunk_y, chunk_z FROM chunks 
        WHERE chunk_x BETWEEN ? AND ? 
        AND chunk_y BETWEEN ? AND ? 
        AND chunk_z BETWEEN ? AND ?;
    )";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) != SQLITE_OK) {
        return chunks;
    }
    
    sqlite3_bind_int(stmt, 1, minChunk.x);
    sqlite3_bind_int(stmt, 2, maxChunk.x);
    sqlite3_bind_int(stmt, 3, minChunk.y);
    sqlite3_bind_int(stmt, 4, maxChunk.y);
    sqlite3_bind_int(stmt, 5, minChunk.z);
    sqlite3_bind_int(stmt, 6, maxChunk.z);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int x = sqlite3_column_int(stmt, 0);
        int y = sqlite3_column_int(stmt, 1);
        int z = sqlite3_column_int(stmt, 2);
        chunks.emplace_back(x, y, z);
    }
    
    sqlite3_finalize(stmt);
    return chunks;
}

bool WorldStorage::beginTransaction() {
    return sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool WorldStorage::commitTransaction() {
    return sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool WorldStorage::rollbackTransaction() {
    return sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

size_t WorldStorage::getChunkCount() {
    if (!db) return 0;
    
    const char* query = "SELECT COUNT(*) FROM chunks;";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    
    size_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
    }
    
    sqlite3_finalize(stmt);
    return count;
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
        std::cerr << "[WORLD_STORAGE] Error clearing world: " << errorMsg << std::endl;
        sqlite3_free(errorMsg);
    }
    
    return success;
}

} // namespace VulkanCube

#endif // HAVE_SQLITE3
