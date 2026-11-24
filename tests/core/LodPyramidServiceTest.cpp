#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

#include "core/Chunk.h"
#include "core/LodChunkMesh.h"
#include "core/LodPyramidService.h"
#include "core/WorldStorage.h"

using namespace Phyxel;
using namespace Phyxel::Core;

namespace {

/// Pure generated terrain: whole cubes only. C3.4's policy must NOT persist this -- the
/// generator reproduces it for free.
std::unique_ptr<Chunk> generatedTerrain(const glm::ivec3& origin = glm::ivec3(0)) {
    auto c = std::make_unique<Chunk>(origin);
    c->initializeForLoading();
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z) {
            const int h = 8 + ((x * 7 + z * 5) % 6);
            for (int y = 0; y < h; ++y)
                c->addCube(glm::ivec3(x, y, z), (y == h - 1) ? "Grass" : "Stone");
        }
    return c;
}

/// Terrain plus a structure. Structures are exactly what far terrain structurally cannot show,
/// so this is the case C3 exists to serve.
std::unique_ptr<Chunk> terrainWithStructure(const glm::ivec3& origin = glm::ivec3(0)) {
    auto c = generatedTerrain(origin);
    for (int x = 10; x < 22; ++x)
        for (int y = 14; y < 20; ++y) c->addCube(glm::ivec3(x, y, 16), "WoodPlanks");
    c->addCube(glm::ivec3(11, 14, 15), "Wood");   // (was subcube detail; the Chunk 1/9 m store went in D3c-5)
    return c;
}

} // namespace

class LodPyramidServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        dbPath = "test_worlds/lod_pyramid_test_" + std::to_string(counter++) + ".db";
        std::remove(dbPath.c_str());
        storage = std::make_unique<WorldStorage>(dbPath);
        ASSERT_TRUE(storage->initialize());
    }
    void TearDown() override {
        storage.reset();
        std::remove(dbPath.c_str());
    }
    std::string dbPath;
    std::unique_ptr<WorldStorage> storage;
};

// --- C3.2: build / persist / invalidate / serve ------------------------------------------

// --- C3.4: only persist what the generator cannot reproduce -------------------------------

TEST_F(LodPyramidServiceTest, DoesNotPersistPlainGeneratedTerrain) {
    auto c = generatedTerrain();
    EXPECT_FALSE(LodPyramidService::shouldPersist(*c))
        << "plain terrain is regenerable from CoarseWorldModel; persisting it stores a coarse "
           "copy of something free";
    EXPECT_EQ(LodPyramidService::buildAndPersist(*c, *storage), 0);
    EXPECT_TRUE(storage->getLodLevels(glm::ivec3(0, 0, 0)).empty());
}

// --- C3.3: the measurement that decides whether C3 beats residency ------------------------

// --- C3.2 wiring: the pyramid must ride along with the ordinary chunk save ----------------

/// Plain terrain saves must NOT write a pyramid (C3.4), or every chunk in the world stores a
/// coarse copy of something the generator makes for free.
TEST_F(LodPyramidServiceTest, SavingPlainTerrainWritesNoPyramid) {
    auto c = generatedTerrain(glm::ivec3(32, 0, 0));        // chunk (1,0,0)
    ASSERT_TRUE(storage->saveChunk(*c));
    EXPECT_TRUE(storage->getLodLevels(glm::ivec3(1, 0, 0)).empty());
}

/// THE PLAN'S GATE: pyramid build must not regress the chunk-edit path beyond the recorded
/// ~40-50 ms/chunk remesh budget (RenderOptimization.md:409). Measured as the DELTA a save
/// costs with the pyramid on vs off, so it isolates the pyramid rather than timing SQLite.
TEST_F(LodPyramidServiceTest, PyramidBuildStaysInsideTheChunkEditBudget) {
    auto c = terrainWithStructure(glm::ivec3(0, 0, 0));
    constexpr int kReps = 12;

    auto timeSaves = [&](bool pyramidOn) {
        WorldStorage::s_lodPyramidOnSave = pyramidOn;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kReps; ++i) storage->saveChunk(*c);
        const auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(t1 - t0).count() / kReps;
    };

    timeSaves(true);                       // warm caches/statements first
    const double off = timeSaves(false);
    const double on  = timeSaves(true);
    WorldStorage::s_lodPyramidOnSave = true;

    const double delta = on - off;
    std::cout << "  save with pyramid " << on << " ms, without " << off
              << " ms, pyramid costs " << delta << " ms/chunk" << std::endl;
    EXPECT_LT(delta, 40.0)
        << "building the pyramid costs " << delta << " ms/chunk, which blows the ~40-50 ms "
           "chunk-edit remesh budget the plan gates on";
}
