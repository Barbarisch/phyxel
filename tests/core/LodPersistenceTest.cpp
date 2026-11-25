#include <gtest/gtest.h>

#include <cstdio>
#include <memory>
#include <string>

#include "core/LodBlobCodec.h"
#include "core/LodBrick.h"
#include <vector>
#include "core/WorldStorage.h"

#include <sqlite3.h>

using namespace Phyxel;
using namespace Phyxel::Core;

namespace {

/// D4: today's Chunk and LodChunkMesh::volumeFromChunk went with the Chunk store. The level-0 volume is built
/// directly by the rule volumeFromChunk applied: a visible cube is full coverage with bulk = skin = its palette id
/// (palette[0] == "" is air, materials in first-seen x, y, z order).
template <class F>
LodVolume cubeVolume(F&& matAt, std::vector<std::string>* outPalette) {
    LodVolume v(glm::ivec3(32, 32, 32), 0);
    std::vector<std::string> palette{""};
    for (int x = 0; x < 32; ++x)
        for (int y = 0; y < 32; ++y)
            for (int z = 0; z < 32; ++z) {
                const std::string m = matAt(x, y, z);
                if (m.empty()) continue;
                uint16_t id = 0;
                for (size_t i = 0; i < palette.size(); ++i) if (palette[i] == m) id = uint16_t(i);
                if (!id) { palette.push_back(m); id = uint16_t(palette.size() - 1); }
                LodCell& c = v.at(x, y, z);
                c.coverage = LodVolume::kFullCoverage;
                c.bulkMaterial = c.skinMaterial = id;
            }
    if (outPalette) *outPalette = std::move(palette);
    return v;
}

/// The terrain chunk the suite used to build (a heightfield, a grass skin), as its level-0 volume.
LodVolume terrainVolume(std::vector<std::string>* palette) {
    return cubeVolume([](int x, int y, int z) -> std::string {
        const int h = 8 + ((x * 7 + z * 5) % 6);
        return y < h ? (y == h - 1 ? "Grass" : "Stone") : "";
    }, palette);
}

} // namespace

/// Each test gets its own on-disk DB; a shared one would let ordering hide bugs.
class LodPersistenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        // A bare filename makes WorldStorage::initialize call create_directories("")
        // which throws; it needs a real parent directory.
        dbPath = "test_worlds/lod_persist_test_" + std::to_string(counter++) + ".db";
        std::remove(dbPath.c_str());
        storage = std::make_unique<WorldStorage>(dbPath);
        ASSERT_TRUE(storage->initialize()) << "SQLite unavailable; this suite needs real storage";
    }
    void TearDown() override {
        storage.reset();
        std::remove(dbPath.c_str());
    }
    std::string dbPath;
    std::unique_ptr<WorldStorage> storage;
};

TEST_F(LodPersistenceTest, RoundTripsAPyramidThroughTheDatabase) {
        std::vector<std::string> palette;
    LodVolume v = terrainVolume(&palette);

    const glm::ivec3 coord(3, 1, -4);
    std::vector<LodVolume> levels;
    for (int lod = 1; lod <= 5; ++lod) {
        v = squash(v, SquashConfig{});
        levels.push_back(v);
        ASSERT_TRUE(storage->saveLodBlob(coord, lod, LodBlobCodec::encode(v, palette)))
            << "failed to persist level " << lod;
    }

    for (int lod = 1; lod <= 5; ++lod) {
        std::vector<uint8_t> blob;
        ASSERT_TRUE(storage->loadLodBlob(coord, lod, blob)) << "level " << lod << " missing";
        LodVolume back;
        std::vector<std::string> pal;
        ASSERT_TRUE(LodBlobCodec::decode(blob.data(), blob.size(), back, pal));
        const LodVolume& want = levels[lod - 1];
        ASSERT_EQ(back.dim(), want.dim()) << "level " << lod;
        ASSERT_EQ(back.level(), want.level()) << "level " << lod;
        for (int x = 0; x < want.dim().x; ++x)
            for (int y = 0; y < want.dim().y; ++y)
                for (int z = 0; z < want.dim().z; ++z)
                    ASSERT_EQ(back.at(x, y, z).coverage, want.at(x, y, z).coverage)
                        << "level " << lod << " cell " << x << "," << y << "," << z;
    }
}

/// Levels are addressed independently: reading level 3 must not require, or return, any other.
/// That independence is the entire reason this is its own table keyed (x,y,z,lod) -- a reader
/// that wants only coarse geometry must never have to pull the full-resolution chunk.
TEST_F(LodPersistenceTest, LevelsAreIndependentlyAddressable) {
        std::vector<std::string> palette;
    LodVolume v = terrainVolume(&palette);
    const glm::ivec3 coord(0, 0, 0);
    for (int lod = 1; lod <= 3; ++lod) {
        v = squash(v, SquashConfig{});
        if (lod == 2) continue;                     // deliberately skip one
        ASSERT_TRUE(storage->saveLodBlob(coord, lod, LodBlobCodec::encode(v, palette)));
    }
    std::vector<uint8_t> blob;
    EXPECT_TRUE(storage->loadLodBlob(coord, 1, blob));
    EXPECT_TRUE(storage->loadLodBlob(coord, 3, blob));
    EXPECT_FALSE(storage->loadLodBlob(coord, 2, blob))
        << "a level that was never written must report absent, not fabricate one";
    EXPECT_TRUE(blob.empty()) << "a failed load must not leave stale bytes in the out param";
    EXPECT_EQ(storage->getLodLevels(coord), (std::vector<int>{1, 3}));
}

/// Different chunks must not collide. A key bug here would serve one chunk's geometry at
/// another position -- terrain that looks plausible but is in the wrong place. Negative
/// coordinates are included deliberately: the world is signed.
TEST_F(LodPersistenceTest, DistinctChunksDoNotCollide) {
        std::vector<std::string> palette;
    LodVolume v = squash(terrainVolume(&palette), SquashConfig{});

    LodVolume other = v;
    other.at(0, 0, 0).coverage = 12345;             // make it distinguishable

    ASSERT_TRUE(storage->saveLodBlob(glm::ivec3(1, 2, 3), 1, LodBlobCodec::encode(v, palette)));
    ASSERT_TRUE(storage->saveLodBlob(glm::ivec3(-1, 2, 3), 1, LodBlobCodec::encode(other, palette)));

    std::vector<uint8_t> a, b;
    ASSERT_TRUE(storage->loadLodBlob(glm::ivec3(1, 2, 3), 1, a));
    ASSERT_TRUE(storage->loadLodBlob(glm::ivec3(-1, 2, 3), 1, b));
    EXPECT_NE(a, b) << "negative and positive chunk coords collided in the key";

    LodVolume back;
    std::vector<std::string> pal;
    ASSERT_TRUE(LodBlobCodec::decode(b.data(), b.size(), back, pal));
    EXPECT_EQ(back.at(0, 0, 0).coverage, 12345u);
}

/// A saved level must be REPLACED, not duplicated, or the pyramid drifts out of sync with the
/// voxels and distant terrain shows an older world.
TEST_F(LodPersistenceTest, ResavingALevelReplacesIt) {
        std::vector<std::string> palette;
    LodVolume v = squash(terrainVolume(&palette), SquashConfig{});
    const glm::ivec3 coord(5, 0, 5);
    ASSERT_TRUE(storage->saveLodBlob(coord, 1, LodBlobCodec::encode(v, palette)));

    v.at(1, 1, 1).coverage = 999;
    ASSERT_TRUE(storage->saveLodBlob(coord, 1, LodBlobCodec::encode(v, palette)));

    EXPECT_EQ(storage->getLodLevels(coord), (std::vector<int>{1})) << "level was duplicated";
    std::vector<uint8_t> blob;
    ASSERT_TRUE(storage->loadLodBlob(coord, 1, blob));
    LodVolume back;
    std::vector<std::string> pal;
    ASSERT_TRUE(LodBlobCodec::decode(blob.data(), blob.size(), back, pal));
    EXPECT_EQ(back.at(1, 1, 1).coverage, 999u) << "read back the OLD level";
}

/// When a chunk's voxels change, its whole pyramid must be droppable in one call. A stale
/// pyramid renders the pre-edit world at distance -- the bug that reads as "the world does not
/// update until I walk up to it".
TEST_F(LodPersistenceTest, DeleteDropsEveryLevelForThatChunkOnly) {
        std::vector<std::string> palette;
    LodVolume v = terrainVolume(&palette);
    const glm::ivec3 doomed(2, 0, 2), keep(9, 0, 9);
    for (int lod = 1; lod <= 4; ++lod) {
        v = squash(v, SquashConfig{});
        ASSERT_TRUE(storage->saveLodBlob(doomed, lod, LodBlobCodec::encode(v, palette)));
        ASSERT_TRUE(storage->saveLodBlob(keep, lod, LodBlobCodec::encode(v, palette)));
    }
    ASSERT_EQ(storage->getLodLevels(doomed).size(), 4u);

    EXPECT_TRUE(storage->deleteLodBlobs(doomed));
    EXPECT_TRUE(storage->getLodLevels(doomed).empty()) << "levels survived the delete";
    EXPECT_EQ(storage->getLodLevels(keep).size(), 4u) << "deleted a neighbour pyramid too";
}

/// Persisted geometry must survive a close/reopen, or none of this helps across sessions.
TEST_F(LodPersistenceTest, SurvivesReopeningTheDatabase) {
        std::vector<std::string> palette;
    LodVolume v = squash(terrainVolume(&palette), SquashConfig{});
    const glm::ivec3 coord(7, 3, 1);
    ASSERT_TRUE(storage->saveLodBlob(coord, 2, LodBlobCodec::encode(v, palette)));

    storage->close();
    storage = std::make_unique<WorldStorage>(dbPath);
    ASSERT_TRUE(storage->initialize());

    std::vector<uint8_t> blob;
    ASSERT_TRUE(storage->loadLodBlob(coord, 2, blob)) << "pyramid did not survive reopen";
    LodVolume back;
    std::vector<std::string> pal;
    EXPECT_TRUE(LodBlobCodec::decode(blob.data(), blob.size(), back, pal));
}

/// MIGRATION: every world.db that already exists predates chunk_lod_blobs. Opening one must
/// ADD the table, not error and not silently leave LOD persistence dead. Simulated by dropping
/// the table from a live DB (exactly the shape of an older world) and reopening.
TEST_F(LodPersistenceTest, AddsTheLodTableToADatabaseThatPredatesIt) {
    ASSERT_NE(storage->getDb(), nullptr);
    char* err = nullptr;
    ASSERT_EQ(sqlite3_exec(storage->getDb(), "DROP TABLE IF EXISTS chunk_lod_blobs;",
                           nullptr, nullptr, &err), SQLITE_OK)
        << (err ? err : "drop failed");

    // Sanity: with the table gone the write path must fail rather than pretend to succeed.
        std::vector<std::string> palette;
    LodVolume v = squash(terrainVolume(&palette), SquashConfig{});
    const glm::ivec3 coord(4, 4, 4);
    EXPECT_FALSE(storage->saveLodBlob(coord, 1, LodBlobCodec::encode(v, palette)))
        << "reported success writing to a table that does not exist";

    // Reopen: createTables() must restore it.
    storage->close();
    storage = std::make_unique<WorldStorage>(dbPath);
    ASSERT_TRUE(storage->initialize()) << "failed to open a DB lacking chunk_lod_blobs";

    EXPECT_TRUE(storage->saveLodBlob(coord, 1, LodBlobCodec::encode(v, palette)))
        << "the LOD table was not restored on open -- LOD persistence would be silently dead "
           "for every pre-existing world";
    std::vector<uint8_t> blob;
    EXPECT_TRUE(storage->loadLodBlob(coord, 1, blob));
    EXPECT_FALSE(blob.empty());
}
