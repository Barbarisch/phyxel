#pragma once

// Conditional SQLite include - disable if SQLite not available
#ifdef HAVE_SQLITE3
#include <sqlite3.h>
#else
// Mock SQLite types when not available
typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
#endif

#include <string>
#include <memory>
#include <glm/glm.hpp>

namespace VulkanCube {

// Forward declarations
class Chunk;
class Cube;
class Subcube;

/**
 * @brief World storage interface using SQLite for efficient chunk persistence
 * 
 * Handles loading/saving chunks from/to SQLite database with optimizations:
 * - Sparse storage (only non-air blocks)
 * - Spatial indexing for fast queries
 * - Chunk streaming for infinite worlds
 * - Transaction batching for performance
 */
class WorldStorage {
public:
    // Constructor/Destructor
    explicit WorldStorage(const std::string& databasePath);
    ~WorldStorage();
    
    // Database management
    bool initialize();
    bool close();
    
    // Chunk operations
    bool saveChunk(const Chunk& chunk);
    bool saveChunk(const Chunk& chunk, bool useTransaction);
    bool loadChunk(const glm::ivec3& chunkCoord, Chunk& chunk);
    bool chunkExists(const glm::ivec3& chunkCoord);
    bool deleteChunk(const glm::ivec3& chunkCoord);
    
    // Individual cube operations
    bool deleteCube(const glm::ivec3& chunkCoord, const glm::ivec3& localPos);
    
    // Batch operations for performance
    bool saveChunks(const std::vector<std::reference_wrapper<const Chunk>>& chunks);
    bool saveDirtyChunks(const std::vector<std::reference_wrapper<Chunk>>& chunks);
    std::vector<glm::ivec3> getChunksInRegion(const glm::ivec3& minChunk, const glm::ivec3& maxChunk);
    
    // World management
    bool createNewWorld();
    bool compactDatabase(); // Vacuum and optimize
    
    // Statistics
    size_t getChunkCount();
    size_t getTotalCubeCount();
    size_t getDatabaseSize();
    
private:
    sqlite3* db = nullptr;
    std::string dbPath;
    
    // Prepared statements for performance
    sqlite3_stmt* insertChunkStmt = nullptr;
    sqlite3_stmt* insertCubeStmt = nullptr;
    sqlite3_stmt* insertSubcubeStmt = nullptr;
    sqlite3_stmt* insertMicrocubeStmt = nullptr;
    sqlite3_stmt* selectChunkStmt = nullptr;
    sqlite3_stmt* selectCubesStmt = nullptr;
    sqlite3_stmt* selectSubcubesStmt = nullptr;
    sqlite3_stmt* selectMicrocubesStmt = nullptr;
    sqlite3_stmt* deleteChunkStmt = nullptr;
    sqlite3_stmt* deleteCubeStmt = nullptr;
    
    // Helper methods
    bool createTables();
    bool prepareStatements();
    void finalizeStatements();
    
    // Transaction helpers
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();
    
    // Conversion helpers
    void saveCube(const Cube& cube, const glm::ivec3& chunkCoord, const glm::ivec3& localPos);
    void saveSubcube(const Subcube& subcube, const glm::ivec3& chunkCoord, 
                     const glm::ivec3& localPos, const glm::ivec3& subPos);
    
    bool loadCubesForChunk(const glm::ivec3& chunkCoord, Chunk& chunk);
    bool loadSubcubesForChunk(const glm::ivec3& chunkCoord, Chunk& chunk);
    bool loadMicrocubesForChunk(const glm::ivec3& chunkCoord, Chunk& chunk);
};

} // namespace VulkanCube
