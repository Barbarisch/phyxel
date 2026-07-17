#include <gtest/gtest.h>

#include <string>

#include "core/Chunk.h"
#include "core/ChunkBlobCodec.h"
#include "core/ChunkVoxelStore.h"
#include "core/Cube.h"

using namespace Phyxel;

// ── Phase 4.2b: palette-store AUTHORITY (docs/LargeWorldScalePlan.md) ────────────
//
// These tests define the authority flip: static voxels live ONLY in ChunkVoxelStore
// (~96 KB/chunk); a heap Cube is materialized on demand solely when a caller asks for
// per-voxel physics/damage state via getCubeAt. Written RED against the 4.2a state
// (where every addCube still allocates a Cube) — the materializedCubeCount()
// assertions are the red line.

namespace {

size_t idxOf(int x, int y, int z) { return static_cast<size_t>(z) + y * 32 + x * 1024; }

}  // namespace

// Fixture: constructs the chunk IN PLACE. (Returning a Chunk by value from a helper leaves the
// voxelManager callbacks bound to the moved-from object — "bad function call".)
class ChunkVoxelAuthority : public ::testing::Test {
protected:
    ChunkVoxelAuthority() : chunk(glm::ivec3(0, 0, 0)) {
        chunk.initializeForLoading();   // wires voxelManager callbacks (no Vulkan needed)
    }
    Chunk chunk;
};

// The core claim: adding static terrain allocates NO Cube objects. Presence, type and
// material all answer from the store.
TEST_F(ChunkVoxelAuthority,AddCubeIsStoreOnly) {
    chunk.addCube(glm::ivec3(1, 2, 3), "Stone");
    chunk.addCube(glm::ivec3(4, 5, 6), "Grass");
    chunk.addCube(glm::ivec3(31, 31, 31), "Dirt");

    EXPECT_EQ(chunk.materializedCubeCount(), 0u)
        << "static voxels must not allocate Cubes after the 4.2b flip";
    EXPECT_EQ(chunk.getVoxelStore().solidCount(), 3u);
    EXPECT_EQ(chunk.getVoxelStore().material(idxOf(1, 2, 3)), "Stone");
    EXPECT_TRUE(chunk.hasVoxelAt(glm::ivec3(1, 2, 3)));
    EXPECT_EQ(chunk.getVoxelType(glm::ivec3(4, 5, 6)), VoxelLocation::CUBE);
    EXPECT_EQ(chunk.getVoxelType(glm::ivec3(0, 0, 0)), VoxelLocation::EMPTY);

    // Presence/type queries must not have materialized anything.
    EXPECT_EQ(chunk.materializedCubeCount(), 0u);
}

// The mesher must produce identical faces from the store alone. Baseline: force-materialize
// every voxel (the pre-flip world) and compare face counts.
TEST_F(ChunkVoxelAuthority,MesherEmitsIdenticalFacesFromStoreOnly) {
    Chunk& storeOnly = chunk;
    Chunk materialized(glm::ivec3(0, 0, 0));
    materialized.initializeForLoading();
    for (int x = 0; x < 8; ++x)
        for (int z = 0; z < 8; ++z) {
            const std::string mat = (x + z) % 2 ? "Stone" : "Grass";
            storeOnly.addCube(glm::ivec3(x, 0, z), mat);
            materialized.addCube(glm::ivec3(x, 0, z), mat);
        }
    // Materialize every voxel of the baseline chunk through the public API.
    for (int x = 0; x < 8; ++x)
        for (int z = 0; z < 8; ++z)
            ASSERT_NE(materialized.getCubeAt(glm::ivec3(x, 0, z)), nullptr);

    ASSERT_EQ(storeOnly.materializedCubeCount(), 0u) << "red line: store-only chunk";

    storeOnly.rebuildFaces();
    materialized.rebuildFaces();

    EXPECT_GT(storeOnly.getFaces().size(), 0u);
    EXPECT_EQ(storeOnly.getFaces().size(), materialized.getFaces().size())
        << "store-only meshing must be pixel-identical to Cube meshing";
    // Meshing itself must not materialize.
    EXPECT_EQ(storeOnly.materializedCubeCount(), 0u);
}

// getCubeAt is the 158-caller compatibility surface: it materializes a real Cube on demand,
// carrying the store's material/visible, at the right position — and physics state written to
// it (damage) survives repeat lookups.
TEST_F(ChunkVoxelAuthority,GetCubeAtMaterializesOnDemand) {
    chunk.addCube(glm::ivec3(7, 8, 9), "Stone");
    ASSERT_EQ(chunk.materializedCubeCount(), 0u) << "red line: no Cube before first getCubeAt";

    Cube* c = chunk.getCubeAt(glm::ivec3(7, 8, 9));
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->getMaterialName(), "Stone");
    EXPECT_TRUE(c->isVisible());
    EXPECT_EQ(c->getPosition(), glm::ivec3(7, 8, 9));
    EXPECT_EQ(chunk.materializedCubeCount(), 1u);

    // Physics-only state lives on the materialized Cube and persists.
    c->addDamage(10.0f);
    Cube* again = chunk.getCubeAt(glm::ivec3(7, 8, 9));
    ASSERT_EQ(again, c) << "repeat lookups must return the same materialized Cube";
    EXPECT_FLOAT_EQ(again->getAccumulatedDamage(), 10.0f);

    // Air must never materialize.
    EXPECT_EQ(chunk.getCubeAt(glm::ivec3(0, 0, 0)), nullptr);
    EXPECT_EQ(chunk.materializedCubeCount(), 1u);
}

// A materialized Cube is an OVERLAY: direct mutation on it wins over the store at scan sites,
// even without an explicit sync call. This is what makes the hybrid read drift-proof. Two
// registry-independent observables: the mesher honors the Cube's visible flag, and the save
// encoder honors the Cube's material.
TEST_F(ChunkVoxelAuthority,HybridReadPrefersMaterializedCube) {
    chunk.addCube(glm::ivec3(3, 3, 3), "Stone");
    chunk.rebuildFaces();
    ASSERT_GT(chunk.getFaces().size(), 0u);

    Cube* c = chunk.getCubeAt(glm::ivec3(3, 3, 3));
    ASSERT_NE(c, nullptr);
    c->setVisible(false);       // no syncVoxelStoreAt on purpose
    c->setMaterial("Gold");

    chunk.rebuildFaces();
    EXPECT_EQ(chunk.getFaces().size(), 0u)
        << "the mesher must honor the materialized Cube's visible flag over the store";

    c->setVisible(true);
    ChunkBlobCodec::Counts counts;
    std::vector<uint8_t> blob = ChunkBlobCodec::encode(chunk, &counts);
    ASSERT_EQ(counts.cubes, 1u);
    Chunk loaded(glm::ivec3(0, 0, 0));
    loaded.initializeForLoading();
    ASSERT_TRUE(ChunkBlobCodec::decode(blob.data(), blob.size(), loaded));
    EXPECT_EQ(loaded.getVoxelStore().material(idxOf(3, 3, 3)), "Gold")
        << "the save encoder must honor the materialized Cube's material over the store";
}

// removeCube must clear BOTH the store entry and any materialized overlay Cube.
TEST_F(ChunkVoxelAuthority,RemoveCubeClearsStoreAndOverlay) {
    chunk.addCube(glm::ivec3(5, 5, 5), "Stone");
    ASSERT_NE(chunk.getCubeAt(glm::ivec3(5, 5, 5)), nullptr);   // materialize

    ASSERT_TRUE(chunk.removeCube(glm::ivec3(5, 5, 5)));
    EXPECT_FALSE(chunk.getVoxelStore().solid(idxOf(5, 5, 5)));
    EXPECT_FALSE(chunk.hasVoxelAt(glm::ivec3(5, 5, 5)));
    EXPECT_EQ(chunk.getCubeAt(glm::ivec3(5, 5, 5)), nullptr);
    EXPECT_EQ(chunk.materializedCubeCount(), 0u);
}

// subdivideAt replaces a solid cube with 27 subcubes. The store entry must go away with it —
// RED even against 4.2a: ChunkVoxelManager::subdivideAt resets the cube slot without erasing
// the store entry (latent mirror gap found during the 4.2b survey).
TEST_F(ChunkVoxelAuthority,SubdivideAtErasesStoreEntry) {
    chunk.addCube(glm::ivec3(6, 6, 6), "Stone");
    ASSERT_TRUE(chunk.getVoxelStore().solid(idxOf(6, 6, 6)));

    ASSERT_TRUE(chunk.subdivideAt(glm::ivec3(6, 6, 6)));
    EXPECT_FALSE(chunk.getVoxelStore().solid(idxOf(6, 6, 6)))
        << "subdivided cell must not linger in the store as a solid cube";
    EXPECT_EQ(chunk.getVoxelType(glm::ivec3(6, 6, 6)), VoxelLocation::SUBDIVIDED);
    // The 27 subcubes must have inherited the parent's material.
    EXPECT_EQ(chunk.getSubcubeAt(glm::ivec3(6, 6, 6), glm::ivec3(1, 1, 1))->getMaterialName(),
              "Stone");
}

// Save/load: the blob codec round-trips a store-only chunk without materializing anything on
// either side.
TEST_F(ChunkVoxelAuthority,BlobRoundTripStaysCubeFree) {
    for (int x = 0; x < 4; ++x)
        for (int y = 0; y < 4; ++y)
            for (int z = 0; z < 4; ++z)
                chunk.addCube(glm::ivec3(x, y, z), (x + y + z) % 2 ? "Stone" : "Dirt");

    ChunkBlobCodec::Counts counts;
    std::vector<uint8_t> blob = ChunkBlobCodec::encode(chunk, &counts);
    EXPECT_EQ(counts.cubes, 64u);
    EXPECT_EQ(chunk.materializedCubeCount(), 0u) << "encode must not materialize";

    Chunk loaded(glm::ivec3(0, 0, 0));
    loaded.initializeForLoading();
    ASSERT_TRUE(ChunkBlobCodec::decode(blob.data(), blob.size(), loaded));
    EXPECT_EQ(loaded.materializedCubeCount(), 0u) << "decode must not materialize";
    EXPECT_EQ(loaded.getVoxelStore().solidCount(), 64u);
    for (int x = 0; x < 4; ++x)
        for (int y = 0; y < 4; ++y)
            for (int z = 0; z < 4; ++z)
                EXPECT_EQ(loaded.getVoxelStore().material(idxOf(x, y, z)),
                          (x + y + z) % 2 ? "Stone" : "Dirt");
}

// Physics occupancy must build from the store alone (characters must not fall through
// never-materialized terrain).
TEST_F(ChunkVoxelAuthority,CollisionShapesBuildFromStoreOnly) {
    for (int x = 0; x < 8; ++x)
        for (int z = 0; z < 8; ++z)
            chunk.addCube(glm::ivec3(x, 0, z), "Stone");
    ASSERT_EQ(chunk.materializedCubeCount(), 0u);

    chunk.buildInitialCollisionShapes();
    EXPECT_EQ(chunk.getCollisionEntityCount(), 64u)
        << "occupancy must see store-only voxels";
    EXPECT_EQ(chunk.materializedCubeCount(), 0u) << "occupancy build must not materialize";
}

// The legacy row-per-voxel DB load hides interior voxels via setVisible(false). Post-flip that
// path writes the store, not a Cube — setCubeVisible is the API for it.
TEST_F(ChunkVoxelAuthority,SetCubeVisibleWritesStoreWithoutMaterializing) {
    chunk.addCube(glm::ivec3(9, 9, 9), "Stone");
    ASSERT_TRUE(chunk.getVoxelStore().visible(idxOf(9, 9, 9)));

    chunk.setCubeVisible(glm::ivec3(9, 9, 9), false);
    EXPECT_FALSE(chunk.getVoxelStore().visible(idxOf(9, 9, 9)));
    EXPECT_EQ(chunk.materializedCubeCount(), 0u)
        << "visibility writes must not allocate a Cube";
    // Still solid (presence unchanged), just hidden.
    EXPECT_TRUE(chunk.hasVoxelAt(glm::ivec3(9, 9, 9)));

    // And it must write through to a Cube that IS materialized.
    Cube* c = chunk.getCubeAt(glm::ivec3(9, 9, 9));
    ASSERT_NE(c, nullptr);
    EXPECT_FALSE(c->isVisible()) << "materialization must carry the store's visible bit";
    chunk.setCubeVisible(glm::ivec3(9, 9, 9), true);
    EXPECT_TRUE(c->isVisible()) << "setCubeVisible must update an existing overlay Cube";
}

// populateWithCubes (32^3 fill) is a Cube-allocation hot spot — it must fill the store instead.
TEST_F(ChunkVoxelAuthority,PopulateWithCubesIsStoreOnly) {
    chunk.populateWithCubes();
    EXPECT_EQ(chunk.getVoxelStore().solidCount(), ChunkVoxelStore::kVoxels);
    EXPECT_EQ(chunk.materializedCubeCount(), 0u);
}
