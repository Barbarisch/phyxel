#include <gtest/gtest.h>
#include "core/WorldStorage.h"
#include "core/Chunk.h"
#include "core/Cube.h"
#include <filesystem>
#include <cstdlib>

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
        if (std::filesystem::exists(dbPath)) {
            std::filesystem::remove(dbPath);
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

} // namespace Testing
} // namespace Phyxel
