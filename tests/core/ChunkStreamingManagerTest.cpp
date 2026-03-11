#include <gtest/gtest.h>
#include "core/ChunkStreamingManager.h"
#include "core/Chunk.h"
#include "core/WorldStorage.h"
#include <glm/glm.hpp>
#include <filesystem>
#include <memory>
#include <vector>

using namespace Phyxel;

// ============================================================================
// Helper: temp database path that auto-cleans
// ============================================================================
class TempDb {
public:
    TempDb() : path(std::filesystem::temp_directory_path() / ("csm_test_" + std::to_string(counter++) + ".db")) {}
    ~TempDb() { std::error_code ec; std::filesystem::remove(path, ec); }
    std::string str() const { return path.string(); }
private:
    std::filesystem::path path;
    static int counter;
};
int TempDb::counter = 0;

// Helper to create a Chunk usable without Vulkan
static std::unique_ptr<Chunk> makeChunk(const glm::ivec3& origin) {
    auto chunk = std::make_unique<Chunk>(origin);
    chunk->initializeForLoading();
    return chunk;
}

// ============================================================================
// Basic Construction / Storage Init Tests
// ============================================================================

TEST(ChunkStreamingManagerTest, WorldStorageNullBeforeInit) {
    ChunkStreamingManager csm;
    EXPECT_EQ(csm.getWorldStorage(), nullptr);
}

TEST(ChunkStreamingManagerTest, InitializeWorldStorageSuccess) {
    TempDb db;
    ChunkStreamingManager csm;
    EXPECT_TRUE(csm.initializeWorldStorage(db.str()));
    EXPECT_NE(csm.getWorldStorage(), nullptr);
}

TEST(ChunkStreamingManagerTest, DoubleInitReplacesPreviousStorage) {
    TempDb db1;
    TempDb db2;
    ChunkStreamingManager csm;
    EXPECT_TRUE(csm.initializeWorldStorage(db1.str()));
    auto* first = csm.getWorldStorage();
    EXPECT_NE(first, nullptr);

    // Second init creates new storage (old is leaked, but tests valid)
    EXPECT_TRUE(csm.initializeWorldStorage(db2.str()));
    EXPECT_NE(csm.getWorldStorage(), nullptr);
    EXPECT_NE(csm.getWorldStorage(), first);
}

// ============================================================================
// SaveChunk Tests
// ============================================================================

TEST(ChunkStreamingManagerTest, SaveChunkNullChunkReturnsFalse) {
    TempDb db;
    ChunkStreamingManager csm;
    csm.initializeWorldStorage(db.str());
    EXPECT_FALSE(csm.saveChunk(nullptr));
}

TEST(ChunkStreamingManagerTest, SaveChunkNoStorageReturnsFalse) {
    ChunkStreamingManager csm;
    auto chunk = makeChunk({0,0,0});
    EXPECT_FALSE(csm.saveChunk(chunk.get()));
}

TEST(ChunkStreamingManagerTest, SaveChunkSingleCube) {
    TempDb db;
    ChunkStreamingManager csm;
    csm.initializeWorldStorage(db.str());

    auto chunk = makeChunk({0,0,0});
    chunk->addCube({5, 5, 5});
    EXPECT_TRUE(csm.saveChunk(chunk.get()));
}

// ============================================================================
// SaveAllChunks / SaveDirtyChunks Tests (require callbacks)
// ============================================================================

class ChunkStreamingCallbackTest : public ::testing::Test {
protected:
    void SetUp() override {
        db = std::make_unique<TempDb>();
        csm.initializeWorldStorage(db->str());

        // Set up callbacks pointing to our local data
        csm.setCallbacks(
            [this](const glm::ivec3&) { /* no-op create */ },
            [this]() -> std::unordered_map<glm::ivec3, Chunk*, ChunkCoordHash>& { return chunkMap; },
            [this]() -> std::vector<std::unique_ptr<Chunk>>& { return chunks; },
            []() { return std::make_pair(VkDevice(nullptr), VkPhysicalDevice(nullptr)); }
        );
    }

    void addChunk(const glm::ivec3& origin) {
        auto chunk = makeChunk(origin);
        chunkMap[origin / 32] = chunk.get();
        chunks.push_back(std::move(chunk));
    }

    // Order matters: csm destroyed before db (reverse declaration order)
    std::unique_ptr<TempDb> db;
    std::unordered_map<glm::ivec3, Chunk*, ChunkCoordHash> chunkMap;
    std::vector<std::unique_ptr<Chunk>> chunks;
    ChunkStreamingManager csm;
};

TEST_F(ChunkStreamingCallbackTest, SaveAllChunksEmpty) {
    EXPECT_TRUE(csm.saveAllChunks());
}

TEST_F(ChunkStreamingCallbackTest, SaveAllChunksWithData) {
    addChunk({0, 0, 0});
    chunks.back()->addCube({1, 1, 1});

    addChunk({32, 0, 0});
    chunks.back()->addCube({2, 2, 2});

    EXPECT_TRUE(csm.saveAllChunks());
}

TEST_F(ChunkStreamingCallbackTest, SaveDirtyChunksNoDirty) {
    addChunk({0, 0, 0});
    chunks.back()->markClean();
    EXPECT_TRUE(csm.saveDirtyChunks());
}

TEST_F(ChunkStreamingCallbackTest, SaveDirtyChunksWithDirty) {
    addChunk({0, 0, 0});
    chunks.back()->addCube({3, 3, 3});  // Makes it dirty
    EXPECT_TRUE(csm.saveDirtyChunks());
}

TEST_F(ChunkStreamingCallbackTest, SaveAllChunksNoStorageReturnsFalse) {
    ChunkStreamingManager noStorage;
    noStorage.setCallbacks(
        [](const glm::ivec3&) {},
        [this]() -> std::unordered_map<glm::ivec3, Chunk*, ChunkCoordHash>& { return chunkMap; },
        [this]() -> std::vector<std::unique_ptr<Chunk>>& { return chunks; },
        []() { return std::make_pair(VkDevice(nullptr), VkPhysicalDevice(nullptr)); }
    );
    EXPECT_FALSE(noStorage.saveAllChunks());
}

TEST_F(ChunkStreamingCallbackTest, SaveDirtyChunksNoStorageReturnsFalse) {
    ChunkStreamingManager noStorage;
    noStorage.setCallbacks(
        [](const glm::ivec3&) {},
        [this]() -> std::unordered_map<glm::ivec3, Chunk*, ChunkCoordHash>& { return chunkMap; },
        [this]() -> std::vector<std::unique_ptr<Chunk>>& { return chunks; },
        []() { return std::make_pair(VkDevice(nullptr), VkPhysicalDevice(nullptr)); }
    );
    EXPECT_FALSE(noStorage.saveDirtyChunks());
}

// ============================================================================
// SaveChunk Round-Trip via WorldStorage
// ============================================================================

TEST(ChunkStreamingManagerTest, SaveAndVerifyViaStorage) {
    TempDb db;
    ChunkStreamingManager csm;
    csm.initializeWorldStorage(db.str());

    auto chunk = makeChunk({0, 0, 0});
    chunk->addCube({10, 10, 10});
    chunk->addCube({5, 5, 5}, "Stone");
    EXPECT_TRUE(csm.saveChunk(chunk.get()));

    // Verify through WorldStorage directly
    auto loadChunk = makeChunk({0, 0, 0});
    glm::ivec3 chunkCoord(0, 0, 0);
    EXPECT_TRUE(csm.getWorldStorage()->loadChunk(chunkCoord, *loadChunk));
    EXPECT_NE(loadChunk->getCubeAt({10, 10, 10}), nullptr);
    EXPECT_NE(loadChunk->getCubeAt({5, 5, 5}), nullptr);
}

// ============================================================================
// UpdateStreaming edge cases (no storage)
// ============================================================================

TEST(ChunkStreamingManagerTest, UpdateStreamingNoStorageDoesNothing) {
    ChunkStreamingManager csm;
    // Should not crash even without storage or callbacks
    csm.updateStreaming(glm::vec3(0.0f), 128.0f, 256.0f);
}
