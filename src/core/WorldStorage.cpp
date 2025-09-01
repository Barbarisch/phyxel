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
bool WorldStorage::deleteCube(const glm::ivec3& chunkCoord, const glm::ivec3& localPos) { return false; }
bool WorldStorage::saveChunks(const std::vector<std::reference_wrapper<const Chunk>>& chunks) { return false; }
bool WorldStorage::saveDirtyChunks(const std::vector<std::reference_wrapper<Chunk>>& chunks) { return false; }
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
    // Ensure directory exists
    std::filesystem::path dbPathObj(dbPath);
    std::filesystem::create_directories(dbPathObj.parent_path());
    
    // Open database
    int result = sqlite3_open(dbPath.c_str(), &db);
    if (result != SQLITE_OK) {
        std::cerr << "[WORLD_STORAGE] Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    
    std::cout << "[WORLD_STORAGE] Opened database: " << dbPath << std::endl;
    
    // Create tables if they don't exist
    if (!createTables()) {
        std::cerr << "[WORLD_STORAGE] Failed to create tables" << std::endl;
        return false;
    }
    
    // Prepare statements
    if (!prepareStatements()) {
        std::cerr << "[WORLD_STORAGE] Failed to prepare statements" << std::endl;
        return false;
    }
    
    std::cout << "[WORLD_STORAGE] WorldStorage initialized successfully" << std::endl;
    return true;
}

bool WorldStorage::close() {
    finalizeStatements();
    
    if (db) {
        sqlite3_close(db);
        db = nullptr;
        std::cout << "[WORLD_STORAGE] Database closed" << std::endl;
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
            color_r REAL NOT NULL,
            color_g REAL NOT NULL,
            color_b REAL NOT NULL,
            is_subdivided INTEGER DEFAULT 0,
            is_visible INTEGER DEFAULT 1,
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
            color_r REAL NOT NULL,
            color_g REAL NOT NULL,
            color_b REAL NOT NULL,
            is_dynamic INTEGER DEFAULT 0,
            PRIMARY KEY (chunk_x, chunk_y, chunk_z, local_x, local_y, local_z, sub_x, sub_y, sub_z),
            FOREIGN KEY (chunk_x, chunk_y, chunk_z, local_x, local_y, local_z) 
                REFERENCES cubes(chunk_x, chunk_y, chunk_z, local_x, local_y, local_z)
        );
        
        -- Spatial indexing for fast chunk queries
        CREATE INDEX IF NOT EXISTS idx_chunks_coord ON chunks(chunk_x, chunk_y, chunk_z);
        CREATE INDEX IF NOT EXISTS idx_cubes_chunk ON cubes(chunk_x, chunk_y, chunk_z);
        CREATE INDEX IF NOT EXISTS idx_subcubes_chunk ON subcubes(chunk_x, chunk_y, chunk_z);
    )";
    
    char* errorMsg = nullptr;
    bool success = sqlite3_exec(db, createTablesSQL, nullptr, nullptr, &errorMsg) == SQLITE_OK;
    
    if (errorMsg) {
        std::cerr << "[WORLD_STORAGE] Error creating tables: " << errorMsg << std::endl;
        sqlite3_free(errorMsg);
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
    
    const char* deleteCubeSQL = R"(
        DELETE FROM cubes WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ? AND local_x = ? AND local_y = ? AND local_z = ?;
    )";
    
    // Prepare all statements
    return (sqlite3_prepare_v2(db, insertChunkSQL, -1, &insertChunkStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, insertCubeSQL, -1, &insertCubeStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, insertSubcubeSQL, -1, &insertSubcubeStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, selectChunkSQL, -1, &selectChunkStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, selectCubesSQL, -1, &selectCubesStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, selectSubcubesSQL, -1, &selectSubcubesStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, deleteChunkSQL, -1, &deleteChunkStmt, nullptr) == SQLITE_OK &&
            sqlite3_prepare_v2(db, deleteCubeSQL, -1, &deleteCubeStmt, nullptr) == SQLITE_OK);
}

void WorldStorage::finalizeStatements() {
    if (insertChunkStmt) { sqlite3_finalize(insertChunkStmt); insertChunkStmt = nullptr; }
    if (insertCubeStmt) { sqlite3_finalize(insertCubeStmt); insertCubeStmt = nullptr; }
    if (insertSubcubeStmt) { sqlite3_finalize(insertSubcubeStmt); insertSubcubeStmt = nullptr; }
    if (selectChunkStmt) { sqlite3_finalize(selectChunkStmt); selectChunkStmt = nullptr; }
    if (selectCubesStmt) { sqlite3_finalize(selectCubesStmt); selectCubesStmt = nullptr; }
    if (selectSubcubesStmt) { sqlite3_finalize(selectSubcubesStmt); selectSubcubesStmt = nullptr; }
    if (deleteChunkStmt) { sqlite3_finalize(deleteChunkStmt); deleteChunkStmt = nullptr; }
    if (deleteCubeStmt) { sqlite3_finalize(deleteCubeStmt); deleteCubeStmt = nullptr; }
}

bool WorldStorage::saveChunk(const Chunk& chunk) {
    return saveChunk(chunk, true); // Use transaction by default for standalone saves
}

bool WorldStorage::saveChunk(const Chunk& chunk, bool useTransaction) {
    if (!db) {
        std::cout << "[WORLD_STORAGE] ERROR: No database connection in saveChunk()" << std::endl;
        return false;
    }
    
    glm::ivec3 chunkCoord = chunk.getWorldOrigin() / 32; // Convert world origin to chunk coordinates
    std::cout << "[WORLD_STORAGE] Starting saveChunk for chunk (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << ")" << std::endl;
    
    bool ownTransaction = false;
    if (useTransaction) {
        if (!beginTransaction()) {
            std::cout << "[WORLD_STORAGE] ERROR: Failed to begin transaction in saveChunk()" << std::endl;
            return false;
        }
        ownTransaction = true;
    }
    
    try {
        // CRITICAL: For dirty chunks, delete existing data first to handle deletions properly
        const char* deleteCubesSQL = "DELETE FROM cubes WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?";
        const char* deleteSubcubesSQL = "DELETE FROM subcubes WHERE chunk_x = ? AND chunk_y = ? AND chunk_z = ?";
        
        sqlite3_stmt* deleteCubesStmt = nullptr;
        sqlite3_stmt* deleteSubcubesStmt = nullptr;
        
        if (sqlite3_prepare_v2(db, deleteCubesSQL, -1, &deleteCubesStmt, nullptr) != SQLITE_OK ||
            sqlite3_prepare_v2(db, deleteSubcubesSQL, -1, &deleteSubcubesStmt, nullptr) != SQLITE_OK) {
            std::cout << "[WORLD_STORAGE] ERROR: Failed to prepare delete statements: " << sqlite3_errmsg(db) << std::endl;
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
        
        std::cout << "[WORLD_STORAGE] Chunk (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z 
                  << ") - Deleted " << deletedCubes << " old cubes, " << deletedSubcubes << " old subcubes" << std::endl;
        
        // Insert/update chunk record
        sqlite3_bind_int(insertChunkStmt, 1, chunkCoord.x);
        sqlite3_bind_int(insertChunkStmt, 2, chunkCoord.y);
        sqlite3_bind_int(insertChunkStmt, 3, chunkCoord.z);
        
        if (sqlite3_step(insertChunkStmt) != SQLITE_DONE) {
            std::cout << "[WORLD_STORAGE] ERROR: Failed to insert chunk record: " << sqlite3_errmsg(db) << std::endl;
            if (ownTransaction) rollbackTransaction();
            return false;
        }
        sqlite3_reset(insertChunkStmt);
        
        int savedCubes = 0;
        
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
                        sqlite3_bind_int(insertCubeStmt, 10, 0); // isSubdivided - always false now (subcubes stored at chunk level)
                        sqlite3_bind_int(insertCubeStmt, 11, cube->isVisible() ? 1 : 0);
                        
                        if (sqlite3_step(insertCubeStmt) != SQLITE_DONE) {
                            std::cout << "[WORLD_STORAGE] ERROR: Failed to insert cube at (" << x << "," << y << "," << z << "): " << sqlite3_errmsg(db) << std::endl;
                            if (ownTransaction) rollbackTransaction();
                            return false;
                        }
                        sqlite3_reset(insertCubeStmt);
                        savedCubes++;
                    }
                }
            }
        }
        
        // Save static subcubes that are stored directly in the chunk
        const auto& staticSubcubes = chunk.getStaticSubcubes();
        int savedSubcubes = 0;
        for (const Subcube* subcube : staticSubcubes) {
            if (subcube && subcube->isVisible()) {
                // Calculate parent cube local position from subcube's world position
                glm::ivec3 parentWorldPos = subcube->getPosition();
                glm::ivec3 parentLocalPos = parentWorldPos - chunkCoord * 32;
                
                // Validate parent position is within chunk bounds
                if (parentLocalPos.x >= 0 && parentLocalPos.x < 32 &&
                    parentLocalPos.y >= 0 && parentLocalPos.y < 32 &&
                    parentLocalPos.z >= 0 && parentLocalPos.z < 32) {
                    
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
                    sqlite3_bind_double(insertSubcubeStmt, 10, subcube->getColor().r);
                    sqlite3_bind_double(insertSubcubeStmt, 11, subcube->getColor().g);
                    sqlite3_bind_double(insertSubcubeStmt, 12, subcube->getColor().b);
                    sqlite3_bind_int(insertSubcubeStmt, 13, subcube->isDynamic() ? 1 : 0);
                    
                    if (sqlite3_step(insertSubcubeStmt) != SQLITE_DONE) {
                        std::cout << "[WORLD_STORAGE] ERROR: Failed to insert static subcube at (" 
                                  << parentLocalPos.x << "," << parentLocalPos.y << "," << parentLocalPos.z << ") sub(" 
                                  << subcube->getLocalPosition().x << "," 
                                  << subcube->getLocalPosition().y << "," 
                                  << subcube->getLocalPosition().z << "): " 
                                  << sqlite3_errmsg(db) << std::endl;
                        if (ownTransaction) rollbackTransaction();
                        return false;
                    }
                    sqlite3_reset(insertSubcubeStmt);
                    savedSubcubes++;
                }
            }
        }
        
        std::cout << "[WORLD_STORAGE] Chunk (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z 
                  << ") - Saved " << savedCubes << " cubes, " << savedSubcubes << " static subcubes" << std::endl;
        
    } catch (...) {
        std::cout << "[WORLD_STORAGE] ERROR: Exception caught in saveChunk()" << std::endl;
        if (ownTransaction) rollbackTransaction();
        return false;
    }
    
    if (ownTransaction) {
        bool commitResult = commitTransaction();
        if (!commitResult) {
            std::cout << "[WORLD_STORAGE] ERROR: Failed to commit transaction in saveChunk()" << std::endl;
        } else {
            std::cout << "[WORLD_STORAGE] Successfully saved chunk (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << ")" << std::endl;
        }
        return commitResult;
    } else {
        std::cout << "[WORLD_STORAGE] Successfully saved chunk (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << ") (no transaction commit needed)" << std::endl;
        return true;
    }
}

bool WorldStorage::loadChunk(const glm::ivec3& chunkCoord, Chunk& chunk) {
    if (!db) return false;
    
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
        
        double r = sqlite3_column_double(selectCubesStmt, 3);
        double g = sqlite3_column_double(selectCubesStmt, 4);
        double b = sqlite3_column_double(selectCubesStmt, 5);
        
        bool isSubdivided = sqlite3_column_int(selectCubesStmt, 6) != 0;
        bool isVisible = sqlite3_column_int(selectCubesStmt, 7) != 0;
        
        if (chunk.addCube(glm::ivec3(x, y, z), glm::vec3(r, g, b))) {
            loadedCubes++;
        }
    }
    
    sqlite3_reset(selectCubesStmt);
    
    // Load subcubes for this chunk
    loadSubcubesForChunk(chunkCoord, chunk);
    
    std::cout << "[WORLD_STORAGE] Loaded " << loadedCubes << " cubes for chunk (" 
              << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << ")" << std::endl;
    
    return loadedCubes > 0;
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
        std::cerr << "[WORLD_STORAGE] Failed to delete cube at (" 
                  << localPos.x << "," << localPos.y << "," << localPos.z 
                  << ") in chunk (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << "): " 
                  << sqlite3_errmsg(db) << std::endl;
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
    
    std::cout << "[WORLD_STORAGE] Processing " << chunks.size() << " chunks for smart save..." << std::endl;
    
    beginTransaction();
    
    for (auto& chunkRef : chunks) {
        Chunk& chunk = chunkRef.get();
        glm::ivec3 chunkCoord = chunk.getWorldOrigin() / 32;
        
        if (chunk.getIsDirty()) {
            std::cout << "[WORLD_STORAGE] Saving DIRTY chunk (" << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << ")" << std::endl;
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
        std::cout << "[WORLD_STORAGE] Smart save complete: " << savedCount << " dirty chunks saved, " 
                  << skippedCount << " clean chunks skipped" << std::endl;
    } else {
        std::cout << "[WORLD_STORAGE] Smart save FAILED!" << std::endl;
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
        std::cerr << "[WORLD_STORAGE] Error compacting database: " << errorMsg << std::endl;
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
        std::cerr << "[WORLD_STORAGE] Error clearing world: " << errorMsg << std::endl;
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
        
        double r = sqlite3_column_double(selectSubcubesStmt, 6);
        double g = sqlite3_column_double(selectSubcubesStmt, 7);
        double b = sqlite3_column_double(selectSubcubesStmt, 8);
        
        bool isDynamic = sqlite3_column_int(selectSubcubesStmt, 9) != 0;
        
        // Add subcube to chunk
        if (chunk.addSubcube(glm::ivec3(x, y, z), glm::ivec3(subX, subY, subZ), glm::vec3(r, g, b))) {
            loadedSubcubes++;
        }
    }
    
    sqlite3_reset(selectSubcubesStmt);
    
    if (loadedSubcubes > 0) {
        std::cout << "[WORLD_STORAGE] Loaded " << loadedSubcubes << " subcubes for chunk (" 
                  << chunkCoord.x << "," << chunkCoord.y << "," << chunkCoord.z << ")" << std::endl;
    }
    
    return true;
}

} // namespace VulkanCube

#endif // HAVE_SQLITE3
