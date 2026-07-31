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

namespace Phyxel {

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

    // --- C3: persisted LOD pyramid (docs/ContinuousLodPlan.md) ---------------------------
    // Coarse geometry for a chunk, stored per LEVEL so a reader fetches only the coarseness
    // it needs. This is what lets a distant region render WITHOUT its full-resolution chunk
    // becoming resident — the R^2 residency wall measured in
    // docs/evidence/lod_residency_wall_20260730.txt.
    bool saveLodBlob(const glm::ivec3& chunkCoord, int lod, const std::vector<uint8_t>& data);
    /// Returns false when there is no row for (chunkCoord, lod) — an absent level is a normal
    /// state (never built, or deliberately not persisted), not an error.
    bool loadLodBlob(const glm::ivec3& chunkCoord, int lod, std::vector<uint8_t>& outData);
    /// Drop every level for a chunk. Called when the chunk's voxels change, because a stale
    /// pyramid would render old geometry at distance — the failure mode that looks like the
    /// world "not updating" until you walk up to it.
    bool deleteLodBlobs(const glm::ivec3& chunkCoord);
    /// Levels present for a chunk, ascending. Empty when nothing is persisted.
    std::vector<int> getLodLevels(const glm::ivec3& chunkCoord);
    
    // Individual cube operations
    bool deleteCube(const glm::ivec3& chunkCoord, const glm::ivec3& localPos);
    
    // Batch operations for performance
    bool saveChunks(const std::vector<std::reference_wrapper<const Chunk>>& chunks);
    bool saveDirtyChunks(const std::vector<std::reference_wrapper<Chunk>>& chunks);
    std::vector<glm::ivec3> getChunksInRegion(const glm::ivec3& minChunk, const glm::ivec3& maxChunk);
    std::vector<glm::ivec3> getAllChunkCoordinates();  // Get coordinates of all chunks in database
    
    // World management
    bool createNewWorld();
    bool compactDatabase(); // Vacuum and optimize
    
    // Statistics
    size_t getChunkCount();
    size_t getTotalCubeCount();
    size_t getDatabaseSize();

    /// Raw SQLite handle (for subsystems that share this database, e.g. ConversationMemory)
    sqlite3* getDb() const { return db; }

    /// Path of the database this storage is bound to (set at construction). Lets callers/tests
    /// identify WHICH world a storage points at, rather than comparing object addresses.
    const std::string& getDbPath() const { return dbPath; }

    // Per-world metadata / generation recipe — a small key/value store that makes a world
    // self-contained (seed, biome layout, extremeness, flora config). See
    // docs/WorldRecipeAndFlora.md. Value is plain text or a JSON blob.
    bool setMeta(const std::string& key, const std::string& value);
    std::string getMeta(const std::string& key) const;   // "" if absent
    bool hasMeta(const std::string& key) const;
    
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
    // Storage v2 (palette+RLE blob per chunk — docs/LargeWorldScalePlan.md Phase 1)
    sqlite3_stmt* insertBlobStmt = nullptr;
    sqlite3_stmt* selectBlobStmt = nullptr;
    sqlite3_stmt* deleteBlobStmt = nullptr;
    // C3: persisted LOD pyramid (chunk_lod_blobs), keyed (x,y,z,lod).
    sqlite3_stmt* insertLodBlobStmt = nullptr;
    sqlite3_stmt* selectLodBlobStmt = nullptr;
    sqlite3_stmt* deleteLodBlobStmt = nullptr;
    sqlite3_stmt* deleteCubeRowsStmt = nullptr;
    sqlite3_stmt* deleteSubcubeRowsStmt = nullptr;
    sqlite3_stmt* deleteMicrocubeRowsStmt = nullptr;

    // Helper methods
    bool createTables();
    bool prepareStatements();
    void finalizeStatements();
    bool applyPragmas();

    // Storage v2 helpers
    bool loadChunkFromBlob(const glm::ivec3& chunkCoord, Chunk& chunk, bool& found);
    bool loadChunkFromLegacyRows(const glm::ivec3& chunkCoord, Chunk& chunk);
    bool deleteLegacyRows(const glm::ivec3& chunkCoord);
    /// One-time v1→v2 migration on open: every chunk that still has
    /// row-format data is re-encoded as a blob; the pre-migration DB is
    /// copied to "<db>.v1.bak" for binary-rollback safety.
    bool migrateLegacyChunks();
    
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

} // namespace Phyxel
