#include <gtest/gtest.h>
#include "core/WorldStorage.h"
#include "core/Chunk.h"
#include "core/Cube.h"
#include "core/Subcube.h"
#include "core/Microcube.h"
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

    // Helper: create a chunk ready for testing (no Vulkan required)
    std::unique_ptr<Chunk> makeChunk(const glm::ivec3& origin) {
        auto chunk = std::make_unique<Chunk>(origin);
        chunk->initializeForLoading();
        return chunk;
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

TEST_F(WorldStorageTest, SaveAndLoadSingleCube) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    // Save a chunk with one cube
    auto chunk = makeChunk(glm::ivec3(0, 0, 0));
    chunk->addCube(glm::ivec3(5, 10, 15));
    EXPECT_TRUE(storage.saveChunk(*chunk));

    // Load into a fresh chunk
    auto loaded = makeChunk(glm::ivec3(0, 0, 0));
    EXPECT_TRUE(storage.loadChunk(glm::ivec3(0, 0, 0), *loaded));

    // Verify the cube is at the right position
    EXPECT_NE(loaded->getCubeAt(glm::ivec3(5, 10, 15)), nullptr);
    // Verify empty positions stay empty
    EXPECT_EQ(loaded->getCubeAt(glm::ivec3(0, 0, 0)), nullptr);

    storage.close();
}

// --- Material round-trip ---

TEST_F(WorldStorageTest, SaveAndLoadCubeWithMaterial) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    auto chunk = makeChunk(glm::ivec3(0, 0, 0));
    chunk->addCube(glm::ivec3(1, 2, 3), "Stone");
    chunk->addCube(glm::ivec3(4, 5, 6), "Metal");
    chunk->addCube(glm::ivec3(7, 8, 9), "Ice");
    EXPECT_TRUE(storage.saveChunk(*chunk));

    auto loaded = makeChunk(glm::ivec3(0, 0, 0));
    EXPECT_TRUE(storage.loadChunk(glm::ivec3(0, 0, 0), *loaded));

    auto* stone = loaded->getCubeAt(glm::ivec3(1, 2, 3));
    auto* metal = loaded->getCubeAt(glm::ivec3(4, 5, 6));
    auto* ice = loaded->getCubeAt(glm::ivec3(7, 8, 9));
    ASSERT_NE(stone, nullptr);
    ASSERT_NE(metal, nullptr);
    ASSERT_NE(ice, nullptr);
    EXPECT_EQ(stone->getMaterialName(), "Stone");
    EXPECT_EQ(metal->getMaterialName(), "Metal");
    EXPECT_EQ(ice->getMaterialName(), "Ice");

    storage.close();
}

TEST_F(WorldStorageTest, DefaultMaterialRoundTrip) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    auto chunk = makeChunk(glm::ivec3(0, 0, 0));
    chunk->addCube(glm::ivec3(0, 0, 0)); // no material specified → "Default"
    EXPECT_TRUE(storage.saveChunk(*chunk));

    auto loaded = makeChunk(glm::ivec3(0, 0, 0));
    EXPECT_TRUE(storage.loadChunk(glm::ivec3(0, 0, 0), *loaded));

    auto* cube = loaded->getCubeAt(glm::ivec3(0, 0, 0));
    ASSERT_NE(cube, nullptr);
    EXPECT_EQ(cube->getMaterialName(), "Default");

    storage.close();
}

// --- Multiple cubes ---

TEST_F(WorldStorageTest, SaveAndLoadManyCubes) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    auto chunk = makeChunk(glm::ivec3(0, 0, 0));

    // Add cubes along the diagonal
    for (int i = 0; i < 32; ++i) {
        chunk->addCube(glm::ivec3(i, i, i), i % 2 == 0 ? "Wood" : "Glass");
    }
    EXPECT_TRUE(storage.saveChunk(*chunk));

    auto loaded = makeChunk(glm::ivec3(0, 0, 0));
    EXPECT_TRUE(storage.loadChunk(glm::ivec3(0, 0, 0), *loaded));

    for (int i = 0; i < 32; ++i) {
        auto* cube = loaded->getCubeAt(glm::ivec3(i, i, i));
        ASSERT_NE(cube, nullptr) << "Missing cube at diagonal position " << i;
        std::string expected = (i % 2 == 0) ? "Wood" : "Glass";
        EXPECT_EQ(cube->getMaterialName(), expected) << "Wrong material at position " << i;
    }

    storage.close();
}

// --- Multi-chunk round-trip ---

TEST_F(WorldStorageTest, SaveAndLoadMultipleChunks) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    // Save two chunks at different coordinates
    auto chunk1 = makeChunk(glm::ivec3(0, 0, 0));
    chunk1->addCube(glm::ivec3(0, 0, 0), "Stone");
    EXPECT_TRUE(storage.saveChunk(*chunk1));

    auto chunk2 = makeChunk(glm::ivec3(32, 0, 0));
    chunk2->addCube(glm::ivec3(1, 1, 1), "Metal");
    EXPECT_TRUE(storage.saveChunk(*chunk2));

    // Load chunk1
    auto loaded1 = makeChunk(glm::ivec3(0, 0, 0));
    EXPECT_TRUE(storage.loadChunk(glm::ivec3(0, 0, 0), *loaded1));
    ASSERT_NE(loaded1->getCubeAt(glm::ivec3(0, 0, 0)), nullptr);
    EXPECT_EQ(loaded1->getCubeAt(glm::ivec3(0, 0, 0))->getMaterialName(), "Stone");

    // Load chunk2
    auto loaded2 = makeChunk(glm::ivec3(32, 0, 0));
    EXPECT_TRUE(storage.loadChunk(glm::ivec3(1, 0, 0), *loaded2));
    ASSERT_NE(loaded2->getCubeAt(glm::ivec3(1, 1, 1)), nullptr);
    EXPECT_EQ(loaded2->getCubeAt(glm::ivec3(1, 1, 1))->getMaterialName(), "Metal");

    storage.close();
}

// --- chunkExists ---

TEST_F(WorldStorageTest, ChunkExistsReturnsTrueForSavedChunk) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    EXPECT_FALSE(storage.chunkExists(glm::ivec3(0, 0, 0)));

    auto chunk = makeChunk(glm::ivec3(0, 0, 0));
    chunk->addCube(glm::ivec3(0, 0, 0));
    storage.saveChunk(*chunk);

    EXPECT_TRUE(storage.chunkExists(glm::ivec3(0, 0, 0)));
    EXPECT_FALSE(storage.chunkExists(glm::ivec3(1, 0, 0)));

    storage.close();
}

// --- deleteChunk ---

TEST_F(WorldStorageTest, DeleteChunkRemovesData) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    auto chunk = makeChunk(glm::ivec3(0, 0, 0));
    chunk->addCube(glm::ivec3(0, 0, 0));
    storage.saveChunk(*chunk);

    EXPECT_TRUE(storage.chunkExists(glm::ivec3(0, 0, 0)));
    EXPECT_TRUE(storage.deleteChunk(glm::ivec3(0, 0, 0)));
    EXPECT_FALSE(storage.chunkExists(glm::ivec3(0, 0, 0)));

    // Loading a deleted chunk should fail
    auto loaded = makeChunk(glm::ivec3(0, 0, 0));
    EXPECT_FALSE(storage.loadChunk(glm::ivec3(0, 0, 0), *loaded));

    storage.close();
}

// --- Overwrite / re-save ---

TEST_F(WorldStorageTest, ResaveChunkOverwritesPrevious) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    // Save chunk with cube at (0,0,0)
    auto chunk = makeChunk(glm::ivec3(0, 0, 0));
    chunk->addCube(glm::ivec3(0, 0, 0), "Wood");
    storage.saveChunk(*chunk);

    // Re-save the same chunk with a different cube
    auto chunk2 = makeChunk(glm::ivec3(0, 0, 0));
    chunk2->addCube(glm::ivec3(1, 1, 1), "Metal");
    storage.saveChunk(*chunk2);

    // Load and verify old cube is gone, new cube is present
    auto loaded = makeChunk(glm::ivec3(0, 0, 0));
    EXPECT_TRUE(storage.loadChunk(glm::ivec3(0, 0, 0), *loaded));
    EXPECT_EQ(loaded->getCubeAt(glm::ivec3(0, 0, 0)), nullptr);
    ASSERT_NE(loaded->getCubeAt(glm::ivec3(1, 1, 1)), nullptr);
    EXPECT_EQ(loaded->getCubeAt(glm::ivec3(1, 1, 1))->getMaterialName(), "Metal");

    storage.close();
}

// --- Empty chunk ---

TEST_F(WorldStorageTest, LoadNonexistentChunkReturnsFalse) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    auto chunk = makeChunk(glm::ivec3(0, 0, 0));
    EXPECT_FALSE(storage.loadChunk(glm::ivec3(99, 99, 99), *chunk));

    storage.close();
}

// --- Statistics ---

TEST_F(WorldStorageTest, GetChunkCountReflectsSaves) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    EXPECT_EQ(storage.getChunkCount(), 0u);

    auto chunk = makeChunk(glm::ivec3(0, 0, 0));
    chunk->addCube(glm::ivec3(0, 0, 0));
    storage.saveChunk(*chunk);
    EXPECT_EQ(storage.getChunkCount(), 1u);

    auto chunk2 = makeChunk(glm::ivec3(32, 0, 0));
    chunk2->addCube(glm::ivec3(0, 0, 0));
    storage.saveChunk(*chunk2);
    EXPECT_EQ(storage.getChunkCount(), 2u);

    storage.close();
}

TEST_F(WorldStorageTest, GetTotalCubeCountReflectsSaves) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    EXPECT_EQ(storage.getTotalCubeCount(), 0u);

    auto chunk = makeChunk(glm::ivec3(0, 0, 0));
    chunk->addCube(glm::ivec3(0, 0, 0));
    chunk->addCube(glm::ivec3(1, 0, 0));
    chunk->addCube(glm::ivec3(2, 0, 0));
    storage.saveChunk(*chunk);
    EXPECT_EQ(storage.getTotalCubeCount(), 3u);

    storage.close();
}

// --- getAllChunkCoordinates ---

TEST_F(WorldStorageTest, GetAllChunkCoordinatesReturnsAllSaved) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    auto c1 = makeChunk(glm::ivec3(0, 0, 0));
    c1->addCube(glm::ivec3(0, 0, 0));
    storage.saveChunk(*c1);

    auto c2 = makeChunk(glm::ivec3(32, 32, 32));
    c2->addCube(glm::ivec3(0, 0, 0));
    storage.saveChunk(*c2);

    auto coords = storage.getAllChunkCoordinates();
    EXPECT_EQ(coords.size(), 2u);

    storage.close();
}

// --- createNewWorld ---

TEST_F(WorldStorageTest, CreateNewWorldClearsAllData) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    auto chunk = makeChunk(glm::ivec3(0, 0, 0));
    chunk->addCube(glm::ivec3(0, 0, 0));
    storage.saveChunk(*chunk);
    EXPECT_EQ(storage.getChunkCount(), 1u);

    EXPECT_TRUE(storage.createNewWorld());
    EXPECT_EQ(storage.getChunkCount(), 0u);
    EXPECT_EQ(storage.getTotalCubeCount(), 0u);

    storage.close();
}

// --- Negative chunk coordinates ---

TEST_F(WorldStorageTest, NegativeChunkCoordinatesRoundTrip) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    glm::ivec3 negOrigin(-32, -64, -32);
    auto chunk = makeChunk(negOrigin);
    chunk->addCube(glm::ivec3(15, 15, 15), "Cork");
    EXPECT_TRUE(storage.saveChunk(*chunk));

    glm::ivec3 chunkCoord = negOrigin / 32;
    auto loaded = makeChunk(negOrigin);
    EXPECT_TRUE(storage.loadChunk(chunkCoord, *loaded));

    auto* cube = loaded->getCubeAt(glm::ivec3(15, 15, 15));
    ASSERT_NE(cube, nullptr);
    EXPECT_EQ(cube->getMaterialName(), "Cork");

    storage.close();
}

// --- Corner positions ---

TEST_F(WorldStorageTest, CubeAtChunkBoundaryPositions) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    auto chunk = makeChunk(glm::ivec3(0, 0, 0));
    chunk->addCube(glm::ivec3(0, 0, 0), "Wood");
    chunk->addCube(glm::ivec3(31, 31, 31), "Stone");
    chunk->addCube(glm::ivec3(0, 31, 0), "Glass");
    EXPECT_TRUE(storage.saveChunk(*chunk));

    auto loaded = makeChunk(glm::ivec3(0, 0, 0));
    EXPECT_TRUE(storage.loadChunk(glm::ivec3(0, 0, 0), *loaded));

    ASSERT_NE(loaded->getCubeAt(glm::ivec3(0, 0, 0)), nullptr);
    ASSERT_NE(loaded->getCubeAt(glm::ivec3(31, 31, 31)), nullptr);
    ASSERT_NE(loaded->getCubeAt(glm::ivec3(0, 31, 0)), nullptr);
    EXPECT_EQ(loaded->getCubeAt(glm::ivec3(0, 0, 0))->getMaterialName(), "Wood");
    EXPECT_EQ(loaded->getCubeAt(glm::ivec3(31, 31, 31))->getMaterialName(), "Stone");
    EXPECT_EQ(loaded->getCubeAt(glm::ivec3(0, 31, 0))->getMaterialName(), "Glass");

    storage.close();
}

// --- Database persistence across close/reopen ---

TEST_F(WorldStorageTest, DataPersistsAcrossCloseAndReopen) {
    {
        WorldStorage storage(dbPath);
        ASSERT_TRUE(storage.initialize());

        auto chunk = makeChunk(glm::ivec3(0, 0, 0));
        chunk->addCube(glm::ivec3(10, 20, 30), "Rubber");
        storage.saveChunk(*chunk);
        storage.close();
    }

    // Reopen
    {
        WorldStorage storage(dbPath);
        ASSERT_TRUE(storage.initialize());

        auto loaded = makeChunk(glm::ivec3(0, 0, 0));
        EXPECT_TRUE(storage.loadChunk(glm::ivec3(0, 0, 0), *loaded));

        auto* cube = loaded->getCubeAt(glm::ivec3(10, 20, 30));
        ASSERT_NE(cube, nullptr);
        EXPECT_EQ(cube->getMaterialName(), "Rubber");

        storage.close();
    }
}

// --- Compact database ---

TEST_F(WorldStorageTest, CompactDatabaseSucceeds) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    auto chunk = makeChunk(glm::ivec3(0, 0, 0));
    chunk->addCube(glm::ivec3(0, 0, 0));
    storage.saveChunk(*chunk);

    EXPECT_TRUE(storage.compactDatabase());

    storage.close();
}

// --- getDatabaseSize ---

TEST_F(WorldStorageTest, GetDatabaseSizeReturnsNonZero) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    auto chunk = makeChunk(glm::ivec3(0, 0, 0));
    chunk->addCube(glm::ivec3(0, 0, 0));
    storage.saveChunk(*chunk);

    EXPECT_GT(storage.getDatabaseSize(), 0u);

    storage.close();
}

// --- All nine materials round-trip ---

TEST_F(WorldStorageTest, AllMaterialsRoundTrip) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    std::vector<std::string> materials = {
        "Wood", "Metal", "Glass", "Rubber", "Stone", "Ice", "Cork", "glow", "Default"
    };

    auto chunk = makeChunk(glm::ivec3(0, 0, 0));
    for (size_t i = 0; i < materials.size(); ++i) {
        chunk->addCube(glm::ivec3(static_cast<int>(i), 0, 0), materials[i]);
    }
    EXPECT_TRUE(storage.saveChunk(*chunk));

    auto loaded = makeChunk(glm::ivec3(0, 0, 0));
    EXPECT_TRUE(storage.loadChunk(glm::ivec3(0, 0, 0), *loaded));

    for (size_t i = 0; i < materials.size(); ++i) {
        auto* cube = loaded->getCubeAt(glm::ivec3(static_cast<int>(i), 0, 0));
        ASSERT_NE(cube, nullptr) << "Missing cube for material: " << materials[i];
        EXPECT_EQ(cube->getMaterialName(), materials[i]);
    }

    storage.close();
}

// ============================================================================
// Storage v2 (palette+RLE blob) — red-before-green suite for
// docs/LargeWorldScalePlan.md Phase 1. These tests are written against the
// PUBLIC WorldStorage API and must fail on the v1 row-per-voxel format:
//   - v1 schema has no tint/state columns → tint/state silently lost
//   - v1 stores ~32k rows per solid chunk → multi-MB database
//   - v1 has no legacy→blob migration on reopen
// ============================================================================

// --- Subcube tint + state round-trip (v1 drops both) ---

TEST_F(WorldStorageTest, SubcubeTintAndStateRoundTrip) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    auto chunk = makeChunk(glm::ivec3(0, 0, 0));
    ASSERT_TRUE(chunk->addSubcube(glm::ivec3(4, 5, 6), glm::ivec3(1, 2, 0),
                                  "Wood", /*tint*/ 0x88CC44u, /*state*/ 3));
    EXPECT_TRUE(storage.saveChunk(*chunk));

    auto loaded = makeChunk(glm::ivec3(0, 0, 0));
    EXPECT_TRUE(storage.loadChunk(glm::ivec3(0, 0, 0), *loaded));

    auto subs = loaded->getStaticSubcubesAt(glm::ivec3(4, 5, 6));
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_EQ(subs[0]->getMaterialName(), "Wood");
    EXPECT_EQ(subs[0]->getLocalPosition(), glm::ivec3(1, 2, 0));
    EXPECT_EQ(subs[0]->getTint(), 0x88CC44u);
    EXPECT_EQ(subs[0]->getState(), 3);

    storage.close();
}

// --- Microcube tint + state round-trip (v1 drops both) ---

TEST_F(WorldStorageTest, MicrocubeTintAndStateRoundTrip) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    auto chunk = makeChunk(glm::ivec3(0, 0, 0));
    ASSERT_TRUE(chunk->addMicrocube(glm::ivec3(10, 11, 12), glm::ivec3(0, 1, 2),
                                    glm::ivec3(2, 0, 1), "Stone",
                                    /*tint*/ 0x336699u, /*state*/ 4));
    EXPECT_TRUE(storage.saveChunk(*chunk));

    auto loaded = makeChunk(glm::ivec3(0, 0, 0));
    EXPECT_TRUE(storage.loadChunk(glm::ivec3(0, 0, 0), *loaded));

    ASSERT_EQ(loaded->getStaticMicrocubeCount(), 1u);
    const auto& micro = loaded->getStaticMicrocubes()[0];
    EXPECT_EQ(micro->getMaterialName(), "Stone");
    EXPECT_EQ(micro->getSubcubeLocalPosition(), glm::ivec3(0, 1, 2));
    EXPECT_EQ(micro->getMicrocubeLocalPosition(), glm::ivec3(2, 0, 1));
    EXPECT_EQ(micro->getTint(), 0x336699u);
    EXPECT_EQ(micro->getState(), 4);

    storage.close();
}

// --- Size gate: a fully solid chunk must be a compact blob, not 32k rows ---
// v1 measured red baseline: ~32,768 rows → multi-MB file. v2 (palette+RLE
// blob): the chunk payload is a handful of runs; the whole DB including
// schema overhead must stay under 256 KB.

TEST_F(WorldStorageTest, SolidChunkDatabaseSizeUnder256KB) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    auto chunk = makeChunk(glm::ivec3(0, 0, 0));
    for (int x = 0; x < 32; ++x)
        for (int y = 0; y < 32; ++y)
            for (int z = 0; z < 32; ++z)
                chunk->addCube(glm::ivec3(x, y, z), "Stone");
    ASSERT_TRUE(storage.saveChunk(*chunk));
    storage.close();

    // Measure the closed main DB file (WAL checkpointed on close).
    size_t fileSize = std::filesystem::file_size(dbPath);
    EXPECT_LE(fileSize, 256u * 1024u)
        << "Solid chunk produced a " << fileSize
        << "-byte DB — row-per-voxel format, not a compact blob";
}

// --- Legacy v1 rows must still load (fallback path) ---

TEST_F(WorldStorageTest, LegacyRowFormatChunkStillLoads) {
    WorldStorage storage(dbPath);
    ASSERT_TRUE(storage.initialize());

    // Craft a v1 row-format chunk directly (bypassing saveChunk, which will
    // write the v2 blob format once Phase 1 lands).
    const char* legacySQL =
        "INSERT INTO chunks (chunk_x, chunk_y, chunk_z) VALUES (2, 0, 3);"
        "INSERT INTO cubes (chunk_x, chunk_y, chunk_z, local_x, local_y, local_z,"
        " is_subdivided, is_visible, material) VALUES (2, 0, 3, 7, 8, 9, 0, 1, 'Metal');"
        "INSERT INTO subcubes (chunk_x, chunk_y, chunk_z, local_x, local_y, local_z,"
        " sub_x, sub_y, sub_z, is_dynamic, material) VALUES (2, 0, 3, 1, 1, 1, 0, 2, 1, 0, 'Glass');";
    char* err = nullptr;
    ASSERT_EQ(sqlite3_exec(storage.getDb(), legacySQL, nullptr, nullptr, &err), SQLITE_OK)
        << (err ? err : "unknown sqlite error");

    auto loaded = makeChunk(glm::ivec3(64, 0, 96));
    EXPECT_TRUE(storage.loadChunk(glm::ivec3(2, 0, 3), *loaded));

    auto* cube = loaded->getCubeAt(glm::ivec3(7, 8, 9));
    ASSERT_NE(cube, nullptr);
    EXPECT_EQ(cube->getMaterialName(), "Metal");
    auto subs = loaded->getStaticSubcubesAt(glm::ivec3(1, 1, 1));
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_EQ(subs[0]->getMaterialName(), "Glass");

    storage.close();
}

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
        auto loaded = makeChunk(glm::ivec3(0, 0, 0));
        EXPECT_TRUE(storage.loadChunk(glm::ivec3(0, 0, 0), *loaded));
        auto* cube = loaded->getCubeAt(glm::ivec3(3, 4, 5));
        ASSERT_NE(cube, nullptr);
        EXPECT_EQ(cube->getMaterialName(), "Ice");

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
