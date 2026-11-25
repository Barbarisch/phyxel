#include <gtest/gtest.h>
#include "core/ChunkStreamingManager.h"
#include "core/ChunkManager.h"
#include "core/WorldStorage.h"
#include "core/WorldGenerator.h"
#include <glm/glm.hpp>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>
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
    ASSERT_NE(csm.getWorldStorage(), nullptr);
    EXPECT_EQ(csm.getWorldStorage()->getDbPath(), db1.str());

    // Second init must REBIND the manager to the new database. Assert that by the database the
    // storage is bound to, not by object address: the first storage is destroyed on re-init, so
    // the allocator may hand the replacement the SAME address — the old `EXPECT_NE(ptr, first)`
    // compared a freed pointer and failed nondeterministically.
    EXPECT_TRUE(csm.initializeWorldStorage(db2.str()));
    ASSERT_NE(csm.getWorldStorage(), nullptr);
    EXPECT_EQ(csm.getWorldStorage()->getDbPath(), db2.str());
}

// (The save / load / stream-in tests over today's Chunk store were retired with it in D4.)
