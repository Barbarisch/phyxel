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
    for (int y = 0; y < 3; ++y)                        // a one-microcube-thick wall detail
        for (int z = 0; z < 3; ++z)
            c->addSubcube(glm::ivec3(11, 14, 15), glm::ivec3(1, y, z), "Wood");
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

TEST_F(LodPyramidServiceTest, BuildAndPersistWritesEveryLevel) {
    auto c = terrainWithStructure();
    EXPECT_EQ(LodPyramidService::buildAndPersist(*c, *storage), LodPyramidService::kMaxLevel);
    EXPECT_EQ(storage->getLodLevels(glm::ivec3(0, 0, 0)),
              (std::vector<int>{1, 2, 3, 4, 5}));
}

/// THE POINT OF C3: geometry for a chunk that is not loaded.
TEST_F(LodPyramidServiceTest, ServesFacesForAChunkThatIsNotResident) {
    auto c = terrainWithStructure();
    ASSERT_GT(LodPyramidService::buildAndPersist(*c, *storage), 0);

    for (int lod = 1; lod <= 3; ++lod) {
        std::vector<InstanceData> faces;
        ASSERT_TRUE(LodPyramidService::facesFromStorage(*storage, glm::ivec3(0, 0, 0), lod, faces))
            << "level " << lod << " unavailable from storage";
        EXPECT_GT(faces.size(), 0u) << "level " << lod << " served zero geometry";
        // The faces must be tagged as LOD cells at the right level, or the shader will decode
        // them as unit cubes and the chunk collapses to a corner.
        for (const InstanceData& f : faces) {
            ASSERT_TRUE(Phyxel::InstanceDataUtils::isLodCell(f.packedData)) << "level " << lod;
            ASSERT_EQ(int(Phyxel::InstanceDataUtils::lodCellLevel(f.packedData)), lod);
        }
    }
}

/// Served geometry must be IDENTICAL to what the resident chunk would mesh. If storage drifts
/// from the live mesher, distant terrain silently disagrees with near terrain.
TEST_F(LodPyramidServiceTest, ServedFacesMatchTheResidentMesherExactly) {
    auto c = terrainWithStructure();
    ASSERT_GT(LodPyramidService::buildAndPersist(*c, *storage), 0);

    for (int lod = 1; lod <= 3; ++lod) {
        std::vector<InstanceData> fromStorage, fromChunk;
        ASSERT_TRUE(LodPyramidService::facesFromStorage(*storage, glm::ivec3(0, 0, 0), lod,
                                                        fromStorage));
        LodChunkMesh::buildForLevel(*c, lod, SquashConfig{}, fromChunk);
        ASSERT_EQ(fromStorage.size(), fromChunk.size()) << "level " << lod << " face count differs";
        for (size_t i = 0; i < fromChunk.size(); ++i) {
            EXPECT_EQ(fromStorage[i].packedData, fromChunk[i].packedData) << "lod " << lod << " f" << i;
            EXPECT_EQ(fromStorage[i].textureIndex, fromChunk[i].textureIndex) << "lod " << lod;
        }
    }
}

/// A stale pyramid renders the pre-edit world at distance. Invalidation must actually clear it.
TEST_F(LodPyramidServiceTest, InvalidateStopsServingStaleGeometry) {
    auto c = terrainWithStructure();
    ASSERT_GT(LodPyramidService::buildAndPersist(*c, *storage), 0);
    std::vector<InstanceData> faces;
    ASSERT_TRUE(LodPyramidService::facesFromStorage(*storage, glm::ivec3(0, 0, 0), 1, faces));

    ASSERT_TRUE(LodPyramidService::invalidate(*storage, glm::ivec3(0, 0, 0)));

    EXPECT_FALSE(LodPyramidService::facesFromStorage(*storage, glm::ivec3(0, 0, 0), 1, faces))
        << "kept serving geometry after the chunk was invalidated";
    EXPECT_TRUE(faces.empty()) << "a failed serve must not leave stale faces in the out param";
    EXPECT_TRUE(storage->getLodLevels(glm::ivec3(0, 0, 0)).empty());
}

/// Chunks are addressed by chunk coordinate, not world origin. Getting that conversion wrong
/// serves the right geometry at the wrong place -- plausible-looking, badly wrong terrain.
TEST_F(LodPyramidServiceTest, UsesChunkCoordinatesNotWorldOrigin) {
    auto c = terrainWithStructure(glm::ivec3(64, 0, -96));   // chunk (2, 0, -3)
    ASSERT_GT(LodPyramidService::buildAndPersist(*c, *storage), 0);

    EXPECT_FALSE(storage->getLodLevels(glm::ivec3(2, 0, -3)).empty())
        << "pyramid was not stored at the chunk coordinate";
    EXPECT_TRUE(storage->getLodLevels(glm::ivec3(64, 0, -96)).empty())
        << "pyramid was stored at the WORLD ORIGIN -- every chunk would land in the wrong cell";
}

// --- C3.4: only persist what the generator cannot reproduce -------------------------------

TEST_F(LodPyramidServiceTest, DoesNotPersistPlainGeneratedTerrain) {
    auto c = generatedTerrain();
    EXPECT_FALSE(LodPyramidService::shouldPersist(*c))
        << "plain terrain is regenerable from CoarseWorldModel; persisting it stores a coarse "
           "copy of something free";
    EXPECT_EQ(LodPyramidService::buildAndPersist(*c, *storage), 0);
    EXPECT_TRUE(storage->getLodLevels(glm::ivec3(0, 0, 0)).empty());
}

TEST_F(LodPyramidServiceTest, PersistsChunksCarryingStructures) {
    auto c = terrainWithStructure();
    EXPECT_TRUE(LodPyramidService::shouldPersist(*c))
        << "a chunk with structure detail MUST be persisted -- far terrain structurally cannot "
           "show structures, which is the mid-field gap C3 exists to close";
    EXPECT_GT(LodPyramidService::buildAndPersist(*c, *storage), 0);
}

// --- C3.3: the measurement that decides whether C3 beats residency ------------------------

/// C3.0 compared blob BYTES to process working set, which is not the same thing. This measures
/// what serving a level actually costs in RAM: decoded volume + face buffer.
TEST_F(LodPyramidServiceTest, ServeCostIsFarBelowResidency) {
    auto c = terrainWithStructure();
    ASSERT_GT(LodPyramidService::buildAndPersist(*c, *storage), 0);

    size_t worstRam = 0;
    for (int lod = 1; lod <= LodPyramidService::kMaxLevel; ++lod) {
        LodPyramidService::ServeCost cost;
        ASSERT_TRUE(LodPyramidService::measureServeCost(*storage, glm::ivec3(0, 0, 0), lod, cost));
        std::cout << "  lod " << lod << ": blob " << cost.blobBytes
                  << " B, volume " << cost.volumeBytes
                  << " B, faces " << cost.faceCount << " (" << cost.faceBytes
                  << " B) -> RAM " << cost.totalRamBytes() << " B\n";
        worstRam = std::max(worstRam, cost.totalRamBytes());
    }
    // A resident chunk costs ~1.28 MB (docs/evidence/lod_residency_wall_20260730.txt; that
    // figure is a 4-point fit with ~2x variance, so this gate is deliberately generous).
    constexpr size_t kResidentChunkBytes = 1280u * 1024u;
    std::cout << "  worst serve cost: " << worstRam << " B vs resident " << kResidentChunkBytes
              << " B\n";
    EXPECT_LT(worstRam, kResidentChunkBytes / 4)
        << "serving a level from storage is not decisively cheaper than keeping the chunk "
           "resident -- C3 does not break the R^2 wall";
}

// --- C3.2 wiring: the pyramid must ride along with the ordinary chunk save ----------------

/// Saving a chunk is the only thing the rest of the engine already does. If the pyramid does
/// not ride along with it, C3 needs a second call site nobody will remember to make.
TEST_F(LodPyramidServiceTest, SavingAChunkPersistsItsPyramid) {
    auto c = terrainWithStructure(glm::ivec3(32, 0, 32));   // chunk (1,0,1)
    ASSERT_TRUE(storage->saveChunk(*c));
    EXPECT_FALSE(storage->getLodLevels(glm::ivec3(1, 0, 1)).empty())
        << "an ordinary saveChunk did not persist the LOD pyramid";

    std::vector<InstanceData> faces;
    EXPECT_TRUE(LodPyramidService::facesFromStorage(*storage, glm::ivec3(1, 0, 1), 2, faces));
    EXPECT_GT(faces.size(), 0u);
}

/// Plain terrain saves must NOT write a pyramid (C3.4), or every chunk in the world stores a
/// coarse copy of something the generator makes for free.
TEST_F(LodPyramidServiceTest, SavingPlainTerrainWritesNoPyramid) {
    auto c = generatedTerrain(glm::ivec3(32, 0, 0));        // chunk (1,0,0)
    ASSERT_TRUE(storage->saveChunk(*c));
    EXPECT_TRUE(storage->getLodLevels(glm::ivec3(1, 0, 0)).empty());
}

/// DEMOLITION. A chunk that had a structure and no longer does must stop being served, or a
/// building you tore down keeps standing at distance.
TEST_F(LodPyramidServiceTest, DemolishingAStructureClearsItsPersistedPyramid) {
    const glm::ivec3 coord(1, 0, 2);
    auto withStruct = terrainWithStructure(coord * 32);
    ASSERT_TRUE(storage->saveChunk(*withStruct));
    ASSERT_FALSE(storage->getLodLevels(coord).empty()) << "precondition: a pyramid exists";

    // Same chunk position, structure gone.
    auto plain = generatedTerrain(coord * 32);
    ASSERT_TRUE(storage->saveChunk(*plain));

    EXPECT_TRUE(storage->getLodLevels(coord).empty())
        << "the demolished structure is still being served at distance";
    std::vector<InstanceData> faces;
    EXPECT_FALSE(LodPyramidService::facesFromStorage(*storage, coord, 2, faces));
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
