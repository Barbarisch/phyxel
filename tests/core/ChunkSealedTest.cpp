#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include "core/Chunk.h"
#include "core/ChunkManager.h"
#include "physics/PhysicsWorld.h"

using namespace Phyxel;

// ── Phase 4.4 stage 2: SEALED chunks (docs/LargeWorldScalePlan.md) ──────────────
//
// A chunk that is uniform-solid AND capped on all six sides by solid neighbour boundary
// layers can never contribute a visible face — it must skip meshing entirely, act as a
// full occluder in the visibility graph, and drop out of the physics grid list (its
// interior is unreachable). Written RED: no seal logic exists yet, so today these chunks
// mesh to zero faces the expensive way, stay all-connected in the occlusion graph, and
// stay registered with physics.

namespace {

// Fill the chunk at `coord` completely solid (O(1) uniform fill).
Chunk* makeSolid(ChunkManager& cm, const glm::ivec3& coord) {
    cm.ensureChunkAt(coord * 32);
    Chunk* c = cm.getChunkAtCoord(coord);
    EXPECT_NE(c, nullptr);
    if (c) c->fillAllCubes("Stone");
    return c;
}

}  // namespace

class ChunkSealedTest : public ::testing::Test {
protected:
    void SetUp() override {
        cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);   // headless (WaterManagerTest pattern)
    }
    // 3x3x3 block of fully solid chunks around the origin; the center one is sealed.
    void buildSolid333() {
        for (int x = -1; x <= 1; ++x)
            for (int y = -1; y <= 1; ++y)
                for (int z = -1; z <= 1; ++z)
                    makeSolid(cm, glm::ivec3(x, y, z));
        remeshAll();
    }
    void remeshAll() {
        for (int x = -1; x <= 1; ++x)
            for (int y = -1; y <= 1; ++y)
                for (int z = -1; z <= 1; ++z)
                    if (Chunk* c = cm.getChunkAtCoord(glm::ivec3(x, y, z)))
                        cm.rebuildChunkFacesWithCrosschunkCulling(*c);
    }
    Chunk* center() { return cm.getChunkAtCoord(glm::ivec3(0, 0, 0)); }

    // Declared BEFORE cm: members destruct in reverse order, so the physics world outlives the
    // chunks that unregister their grids from it in ~Chunk (else use-after-free at teardown).
    Phyxel::Physics::PhysicsWorld physics;
    ChunkManager cm;   // headless: createVulkanBuffer no-ops without a device
};

// The core claim: a capped uniform-solid chunk is detected as sealed, emits no faces, and
// blocks sight in the occlusion graph.
TEST_F(ChunkSealedTest, CenterOfSolid333IsSealed) {
    buildSolid333();
    Chunk* c = center();
    ASSERT_NE(c, nullptr);

    EXPECT_TRUE(c->isSealed()) << "capped uniform-solid chunk must classify as sealed";
    EXPECT_TRUE(c->getFaces().empty());
    // Sealed = fully blocking: no chunk-face pair is mutually visible through it.
    for (int a = 0; a < 6; ++a)
        for (int b = 0; b < 6; ++b)
            EXPECT_FALSE(c->facesConnected(a, b))
                << "sealed chunk must occlude " << a << "->" << b;
}

// Chunks with any uncapped side (the outer shell here) must NOT seal — they mesh their
// exposed boundary as today.
TEST_F(ChunkSealedTest, ShellChunksAreNotSealed) {
    buildSolid333();
    Chunk* corner = cm.getChunkAtCoord(glm::ivec3(1, 1, 1));
    ASSERT_NE(corner, nullptr);
    EXPECT_FALSE(corner->isSealed());
    EXPECT_GT(corner->getFaces().size(), 0u) << "exposed boundary must still mesh";
}

// Pure-air chunks are the other uniform case: never sealed (nothing to occlude — sight
// passes), no faces.
TEST_F(ChunkSealedTest, AirChunkIsNotSealedAndStaysTransparentToSight) {
    cm.ensureChunkAt(glm::ivec3(0, 0, 0));
    Chunk* c = center();
    ASSERT_NE(c, nullptr);
    cm.rebuildChunkFacesWithCrosschunkCulling(*c);

    EXPECT_FALSE(c->isSealed());
    EXPECT_TRUE(c->getFaces().empty());
    for (int a = 0; a < 6; ++a)
        for (int b = 0; b < 6; ++b)
            EXPECT_TRUE(c->facesConnected(a, b)) << "air must stay sight-transparent";
}

// Removing a voxel INSIDE a sealed chunk unseals it on the next managed rebuild and the
// interior cavity meshes.
TEST_F(ChunkSealedTest, InteriorEditUnseals) {
    buildSolid333();
    Chunk* c = center();
    ASSERT_NE(c, nullptr);
    ASSERT_TRUE(c->isSealed());

    ASSERT_TRUE(c->removeCube(glm::ivec3(15, 15, 15)));
    cm.rebuildChunkFacesWithCrosschunkCulling(*c);

    EXPECT_FALSE(c->isSealed());
    EXPECT_EQ(c->getFaces().size(), 6u) << "a 1-voxel cavity exposes exactly 6 faces";
}

// Removing a NEIGHBOUR's boundary voxel (the cell touching the center's top face) must
// unseal the center — its top face cell is now exposed.
TEST_F(ChunkSealedTest, NeighborBoundaryEditUnseals) {
    buildSolid333();
    Chunk* c = center();
    Chunk* above = cm.getChunkAtCoord(glm::ivec3(0, 1, 0));
    ASSERT_NE(c, nullptr);
    ASSERT_NE(above, nullptr);
    ASSERT_TRUE(c->isSealed());

    // Local (5, 0, 5) in the chunk above sits directly on the center chunk's +Y boundary.
    ASSERT_TRUE(above->removeCube(glm::ivec3(5, 0, 5)));
    cm.rebuildChunkFacesWithCrosschunkCulling(*above);
    cm.rebuildChunkFacesWithCrosschunkCulling(*c);

    EXPECT_FALSE(c->isSealed());
    EXPECT_GT(c->getFaces().size(), 0u) << "the newly exposed top cell must mesh";
}

// L3 stress (plan 4.4 gate): dig a 1-voxel shaft straight DOWN through three sealed bands.
// After EVERY removed voxel the ground query must land exactly on the new shaft floor — the
// unseal chain has no window where a character would fall through an unregistered grid.
TEST_F(ChunkSealedTest, DigShaftThroughThreeSealedBands) {
    ASSERT_TRUE(physics.initialize());
    cm.setPhysicsWorld(&physics);

    // A 3x3 column footprint of solid chunks, bands y=-1..3, air above. The y=-1 floor layer
    // exists purely so band 0's bottom face is capped (it is neither meshed nor given physics).
    // The interior column (0, y, 0) is capped on all sides for bands 0..2; band 3's top is
    // open sky.
    for (int x = -1; x <= 1; ++x)
        for (int z = -1; z <= 1; ++z)
            for (int y = -1; y <= 3; ++y)
                makeSolid(cm, glm::ivec3(x, y, z));
    for (int x = -1; x <= 1; ++x)
        for (int z = -1; z <= 1; ++z)
            for (int y = 0; y <= 3; ++y) {
                Chunk* c = cm.getChunkAtCoord(glm::ivec3(x, y, z));
                ASSERT_NE(c, nullptr);
                c->setPhysicsWorld(&physics);
                c->createChunkPhysicsBody();
                cm.rebuildChunkFacesWithCrosschunkCulling(*c);
            }
    // Bands 0..2 of the center column are sealed; band 3 is exposed (open sky above).
    for (int y = 0; y <= 2; ++y)
        ASSERT_TRUE(cm.getChunkAtCoord(glm::ivec3(0, y, 0))->isSealed()) << "band " << y;
    ASSERT_FALSE(cm.getChunkAtCoord(glm::ivec3(0, 3, 0))->isSealed());

    auto* vw = physics.getVoxelWorld();
    ASSERT_NE(vw, nullptr);

    // Dig at world column (16, ?, 16) from the top surface (y=127) down through band 1
    // (crossing two chunk seams). Invariant at EVERY step: the ground under the shaft is
    // exactly the voxel below the last one removed.
    const glm::vec3 probe(16.5f, 130.0f, 16.5f);
    for (int wy = 127; wy >= 40; --wy) {
        // removeCubeFast defers the re-mesh (the invariant under test is COLLISION, which
        // updates through the occupancy callbacks either way) — the immediate-remesh form
        // would make this a minutes-long test for no extra coverage.
        ASSERT_TRUE(cm.removeCubeFast(glm::ivec3(16, wy, 16))) << "remove failed at y=" << wy;
        const float ground = vw->findGroundY(probe, 0.3f, 256.0f);
        EXPECT_NEAR(ground, static_cast<float>(wy), 0.6f)
            << "ground must track the shaft floor after removing y=" << wy;
    }
    // The dig crossed into bands 3, 2 and 1 — all must be unsealed with live collision.
    EXPECT_FALSE(cm.getChunkAtCoord(glm::ivec3(0, 3, 0))->isSealed());
    EXPECT_FALSE(cm.getChunkAtCoord(glm::ivec3(0, 2, 0))->isSealed());
    EXPECT_FALSE(cm.getChunkAtCoord(glm::ivec3(0, 1, 0))->isSealed());
    // Band 0 was never touched: still sealed, still absent from the query list.
    EXPECT_TRUE(cm.getChunkAtCoord(glm::ivec3(0, 0, 0))->isSealed());
}

// Physics: sealed chunks leave the dynamics-world grid list (interior unreachable);
// unsealing re-registers them.
TEST_F(ChunkSealedTest, SealedChunkLeavesPhysicsGridList) {
    ASSERT_TRUE(physics.initialize());
    cm.setPhysicsWorld(&physics);

    buildSolid333();
    // Register collision for all 27 (mirrors the streaming finalize).
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            for (int z = -1; z <= 1; ++z) {
                Chunk* c = cm.getChunkAtCoord(glm::ivec3(x, y, z));
                ASSERT_NE(c, nullptr);
                c->setPhysicsWorld(&physics);
                c->createChunkPhysicsBody();
            }
    auto* vw = physics.getVoxelWorld();
    ASSERT_NE(vw, nullptr);
    const size_t before = vw->gridCount();
    ASSERT_EQ(before, 27u);

    remeshAll();   // seal evaluation runs here
    EXPECT_EQ(vw->gridCount(), 26u) << "the sealed center grid must be unregistered";

    // Unseal via an interior edit → the managed rebuild must re-register it.
    Chunk* c = center();
    ASSERT_TRUE(c->removeCube(glm::ivec3(10, 10, 10)));
    cm.rebuildChunkFacesWithCrosschunkCulling(*c);
    EXPECT_EQ(vw->gridCount(), 27u) << "unsealing must re-register the grid";
}
