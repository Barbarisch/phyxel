#pragma once
#include "core/voxfound/StorageLab.h"

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
    // (saveChunk / loadChunk of today's Chunk objects went with D4; stored chunks are read as PXB2 blobs and
    // converted by Core::VoxFound::convertPxb2.)
    /// The raw v2 blob of a chunk (a copy), so a caller can decode it off this thread
    /// (ChunkStreamingManager::loadChunksParallel). False when the chunk has no blob row
    /// (absent, or legacy v1 rows only).
    bool readChunkBlob(const glm::ivec3& chunkCoord, std::vector<uint8_t>& out);
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
    /// Every chunk coordinate that has ANY persisted LOD level. This is what lets the renderer
    /// iterate the chunks that actually HAVE coarse geometry instead of probing a volume of
    /// speculative coordinates -- the difference between a handful of rows and 25^3 = 15,625
    /// misses per scan, and it removes the need for an arbitrary reach cap.
    std::vector<glm::ivec3> getChunksWithLodBlobs();

    /// Levels present for a chunk, ascending. Empty when nothing is persisted.
    std::vector<int> getLodLevels(const glm::ivec3& chunkCoord);

    /// VoxelFoundationPlan F3.3: the new store's chunks (codec v3, 'PXF3'), table voxfound_chunk_blobs. Only chunks
    /// that differ from the generator are written; a row wins over the generator when its region is built.
    bool saveVoxFoundBlob(const glm::ivec3& chunkCoord, const std::vector<uint8_t>& data);
    bool loadVoxFoundBlob(const glm::ivec3& chunkCoord, std::vector<uint8_t>& outData);
    bool deleteVoxFoundBlob(const glm::ivec3& chunkCoord);
    std::vector<glm::ivec3> getVoxFoundBlobCoords();
    
    // Individual cube operations
    bool deleteCube(const glm::ivec3& chunkCoord, const glm::ivec3& localPos);
    
    // Batch operations for performance
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
    // docs/WorldModel.md. Value is plain text or a JSON blob.
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
    bool deleteLegacyRows(const glm::ivec3& chunkCoord);
    /// One-time v1→v2 migration on open: every chunk that still has
    /// row-format data is re-encoded as a blob; the pre-migration DB is
    /// copied to "<db>.v1.bak" for binary-rollback safety.
    bool migrateLegacyChunks();
public:
    /// docs/VoxelFoundationPlan.md D3c-2: one v1 chunk's rows (no Chunk object), encoded by encodePxb2FromRows
    /// (the migration's encoder; the legacy Chunk + ChunkBlobCodec::encode A/B went with D3c-5).
    bool readLegacyRows(const glm::ivec3& chunkCoord, Core::VoxFound::V1ChunkRows& out);
    std::vector<glm::ivec3> legacyChunkCoords();
    static inline bool s_skipMigration = false;     // tests: open a v1 DB without migrating it (the A/B reads rows)
private:
    
    // Transaction helpers
    bool beginTransaction();
    bool commitTransaction();
    bool rollbackTransaction();
    
};

} // namespace Phyxel
