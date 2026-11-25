#include <gtest/gtest.h>
#include "core/WorldStorage.h"
#include "core/voxfound/StorageLab.h"
#include "core/Cube.h"
#include <filesystem>
#include <cstdlib>
#include <sqlite3.h>

namespace Phyxel {
namespace Testing {

class WorldStorageTest : public ::testing::Test {
protected:
    std::string dbPath;

    void SetUp() override {
        std::string filename = "test_world_storage_" + std::to_string(std::rand()) + ".db";
        dbPath = (std::filesystem::current_path() / filename).string();
        
        if (std::filesystem::exists(dbPath)) {
            std::filesystem::remove(dbPath);
        }
    }

    void TearDown() override {
        // Main DB plus WAL sidecars and the migration backup.
        for (const auto& suffix : {"", "-wal", "-shm", ".v1.bak"}) {
            std::string path = dbPath + suffix;
            if (std::filesystem::exists(path)) {
                std::filesystem::remove(path);
            }
        }
    }
};

// --- Initialization ---

TEST_F(WorldStorageTest, InitializeCreatesTables) {
    WorldStorage storage(dbPath);
    EXPECT_TRUE(storage.initialize());
    storage.close();
}

TEST_F(WorldStorageTest, InitializeTwiceSucceeds) {
    WorldStorage storage(dbPath);
    EXPECT_TRUE(storage.initialize());
    storage.close();

    // Re-open the same DB
    WorldStorage storage2(dbPath);
    EXPECT_TRUE(storage2.initialize());
    storage2.close();
}

// --- Single cube round-trip ---

// --- Material round-trip ---

// --- Multiple cubes ---

// --- Multi-chunk round-trip ---

// --- chunkExists ---

// --- deleteChunk ---

// --- Overwrite / re-save ---

// --- Empty chunk ---

// --- Statistics ---

// --- getAllChunkCoordinates ---

// --- createNewWorld ---

// --- Negative chunk coordinates ---

// --- Corner positions ---

// --- Database persistence across close/reopen ---

// --- Compact database ---

// --- getDatabaseSize ---

// --- All nine materials round-trip ---

// ============================================================================
// Storage v2 (palette+RLE blob) — red-before-green suite for
// docs/LargeWorldScalePlan.md Phase 1. These tests are written against the
// PUBLIC WorldStorage API and must fail on the v1 row-per-voxel format:
//   - v1 schema has no tint/state columns → tint/state silently lost
//   - v1 stores ~32k rows per solid chunk → multi-MB database
//   - v1 has no legacy→blob migration on reopen
// ============================================================================

// --- Subcube tint + state round-trip (v1 drops both) ---

// --- Microcube tint + state round-trip (v1 drops both) ---

// --- Size gate: a fully solid chunk must be a compact blob, not 32k rows ---
// v1 measured red baseline: ~32,768 rows → multi-MB file. v2 (palette+RLE
// blob): the chunk payload is a handful of runs; the whole DB including
// schema overhead must stay under 256 KB.

// --- Legacy v1 rows must still load (fallback path) ---

// --- Reopen migrates legacy rows to blobs (one-time, with .bak backup) ---

TEST_F(WorldStorageTest, LegacyRowsMigrateToBlobsOnReopen) {
    {
        WorldStorage storage(dbPath);
        ASSERT_TRUE(storage.initialize());
        const char* legacySQL =
            "INSERT INTO chunks (chunk_x, chunk_y, chunk_z) VALUES (0, 0, 0);"
            "INSERT INTO cubes (chunk_x, chunk_y, chunk_z, local_x, local_y, local_z,"
            " is_subdivided, is_visible, material) VALUES (0, 0, 0, 3, 4, 5, 0, 1, 'Ice');";
        char* err = nullptr;
        ASSERT_EQ(sqlite3_exec(storage.getDb(), legacySQL, nullptr, nullptr, &err), SQLITE_OK)
            << (err ? err : "unknown sqlite error");
        storage.close();
    }

    // Reopen: initialize() must migrate the legacy rows into blob format.
    {
        WorldStorage storage(dbPath);
        ASSERT_TRUE(storage.initialize());

        // Data survives the migration...
        // D4: read through the blob and the converter (today's Chunk + loadChunk went with D4).
        std::vector<uint8_t> blob;
        ASSERT_TRUE(storage.readChunkBlob(glm::ivec3(0, 0, 0), blob));
        Phyxel::Core::VoxFound::BlockStore s;
        std::vector<std::string> names;
        std::string err;
        ASSERT_TRUE(Phyxel::Core::VoxFound::convertPxb2(blob.data(), blob.size(), s, names, nullptr, &err)) << err;
        uint8_t m = 0;
        ASSERT_TRUE(s.uniformBlock(glm::ivec3(3, 4, 5), m)) << "the migrated cube is one whole block";
        ASSERT_LT(size_t(m), names.size());
        EXPECT_EQ(names[m], "Ice");

        // ...and the legacy rows are gone (replaced by the blob).
        sqlite3_stmt* stmt = nullptr;
        ASSERT_EQ(sqlite3_prepare_v2(storage.getDb(),
                                     "SELECT COUNT(*) FROM cubes;", -1, &stmt, nullptr),
                  SQLITE_OK);
        ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
        int legacyRows = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        EXPECT_EQ(legacyRows, 0) << "legacy cube rows were not migrated to blobs";

        storage.close();
    }

    // A rollback-safety backup of the pre-migration DB must exist.
    EXPECT_TRUE(std::filesystem::exists(dbPath + ".v1.bak"));
    std::filesystem::remove(dbPath + ".v1.bak");
}

// --- WAL journal mode must be active (PRAGMA tuning) ---

TEST_F(WorldStorageTest, DatabaseUsesWALJournalMode) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    sqlite3_stmt* stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(storage.getDb(), "PRAGMA journal_mode;", -1, &stmt, nullptr),
              SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    std::string mode = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);
    EXPECT_EQ(mode, "wal");

    storage.close();
}

} // namespace Testing
} // namespace Phyxel
