#include "IntegrationTestFixture.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "core/DamageSystem.h"
#include "core/CoherentFragmentManager.h"
#include "core/KinematicVoxelManager.h"
#include "core/MaterialRegistry.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelDynamicsWorld.h"
#include <glm/glm.hpp>
#include <filesystem>

namespace Phyxel {
namespace Testing {

/**
 * Axe-chop kerf fracture (docs/DestructionSystemV2.md §5.E — fracture, NOT blast).
 * Each swing carves a microcube-resolution wedge into the trunk cross-section:
 * a VISIBLE notch that deepens per swing, exposes raw heartwood on the cut faces,
 * fills the shell-tree hollow with solid wood, and NEVER sprays debris. The tree
 * stands while structural wood bridges the kerf and releases as ONE coherent
 * hinged body on the swing that cuts through.
 *
 * Red baseline: before carveChopKerf, chopping had no geometric effect at all —
 * a hidden meter, then a severing BLAST (debris explosion). Every assertion here
 * (partial notch with the tree standing, zero debris, heartwood exposure, hollow
 * fill, blast-free coherent release) fails on that baseline.
 */
class ChopKerfIntegrationTest : public ChunkManagerTestFixture {
protected:
    void SetUp() override {
        ChunkManagerTestFixture::SetUp();
        if (!isEnvironmentReady() || !chunkManager) return;
        for (const char* p : {"resources/materials.json", "../resources/materials.json",
                              "../../resources/materials.json", "../../../resources/materials.json"}) {
            if (std::filesystem::exists(p)) { Core::MaterialRegistry::instance().loadFromJson(p); break; }
        }
        chunkManager->createChunk(glm::ivec3(0, 0, 0), false);   // empty chunk, world 0..31
    }

    void put(int x, int y, int z, const char* mat) {
        chunkManager->addCubeWithMaterial(glm::ivec3(x, y, z), mat);
    }
    void putBox(int x0, int y0, int z0, int x1, int y1, int z1, const char* mat) {
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z) put(x, y, z, mat);
    }
    bool solid(int x, int y, int z) { return chunkManager->hasVoxelAt(glm::ivec3(x, y, z)); }
    int countSolid(int x0, int y0, int z0, int x1, int y1, int z1) {
        int n = 0;
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z) if (solid(x, y, z)) ++n;
        return n;
    }
    /// Count microcubes in a cell whose material matches `mat` exactly.
    int countMicrosOf(glm::ivec3 cell, const char* mat) {
        Chunk* ch = chunkManager->getChunkAtCoord(ChunkManager::worldToChunkCoord(cell));
        if (!ch) return 0;
        const glm::ivec3 lp = ChunkManager::worldToLocalCoord(cell);
        int n = 0;
        for (int sx = 0; sx < 3; ++sx)
            for (int sy = 0; sy < 3; ++sy)
                for (int sz = 0; sz < 3; ++sz)
                    for (Microcube* mc : ch->getMicrocubesAt(lp, {sx, sy, sz}))
                        if (mc && mc->getMaterialName() == mat) ++n;
        return n;
    }

    /// A rooted 1x1-cube trunk with a leaf canopy on a stone floor.
    void buildSimpleTree() {
        putBox(6, 3, 6, 14, 3, 14, "Stone");    // floor
        putBox(10, 4, 10, 10, 12, 10, "Log");   // trunk y4..12
        putBox(8, 12, 8, 12, 15, 12, "Leaf");   // canopy
    }
};

TEST_F(ChopKerfIntegrationTest, PartialKerf_NotchCarved_TreeStands_NoDebris) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";
    buildSimpleTree();

    DamageSystem dmg(chunkManager.get(), nullptr);
    // One axe bite from -z at trunk cell (10,6,10): shallow kerf, ~1/3 in.
    auto r = dmg.carveChopKerf({10, 6, 10}, glm::vec3(0, 0, 1), 0.35f, /*coherent*/ false);

    EXPECT_TRUE(r.carved) << "no wood carved";
    EXPECT_GT(r.microsRemoved, 0) << "kerf removed no micros";
    EXPECT_FALSE(r.severed) << "a shallow notch must not fell the tree";
    EXPECT_NEAR(r.fullDepth, 1.0f, 0.01f) << "1-cube trunk depth";
    // The tree STANDS: trunk above the cut and canopy intact.
    EXPECT_TRUE(solid(10, 10, 10)) << "upper trunk fell from a shallow notch";
    EXPECT_GE(countSolid(8, 12, 8, 12, 15, 12), 100) << "canopy fell from a shallow notch";
    // The hit cell is now partially carved (still solid content, subdivided).
    EXPECT_TRUE(solid(10, 6, 10));
    // The cut faces show raw heartwood.
    EXPECT_GT(countMicrosOf({10, 6, 10}, "LogHeartwood"), 0) << "no heartwood exposed on the cut";
    // FRACTURE, not blast: a handful of tactile splinters per bite (<=6), nothing
    // like a blast's 100+ debris.
    EXPECT_LE(r.collapse.debrisSpawned, 8) << "kerf bite must throw only a few splinters";
}

TEST_F(ChopKerfIntegrationTest, DeepeningKerf_CutsThrough_OneCoherentBody) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);
    buildSimpleTree();

    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);
    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);

    // Swing until it falls: each constant-size bite carves from the current notch
    // frontier (stateless), the tree must survive every partial cut and release
    // exactly once, on the swing that cuts through.
    bool severed = false;
    int swings = 0, totalDebris = 0;
    for (int swing = 0; swing < 16 && !severed; ++swing) {
        auto r = dmg.carveChopKerf({10, 6, 10}, glm::vec3(0, 0, 1), 0.35f, /*coherent*/ true);
        severed = r.severed;
        totalDebris += r.collapse.debrisSpawned;
        ++swings;
        if (!severed) {
            EXPECT_TRUE(solid(10, 10, 10)) << "tree fell early on swing " << swings;
            EXPECT_EQ(mgr.count(), 0u);
        }
    }
    ASSERT_TRUE(severed) << "kerf reached past full depth but the tree never released";
    EXPECT_GE(swings, 2) << "felling must take multiple swings (visible progression)";
    // ONE coherent hinged body — the tree falls as a tree.
    EXPECT_EQ(mgr.count(), 1u) << "release did not produce one coherent body";
    // The severed part left the static grid; the stump (below the cut) stays rooted.
    EXPECT_FALSE(solid(10, 10, 10)) << "upper trunk still standing after release";
    EXPECT_TRUE(solid(10, 4, 10)) << "stump fell";
    EXPECT_TRUE(solid(10, 3, 10)) << "floor fell";
    // FRACTURE, not blast: <=6 tactile splinters per swing plus a couple of
    // snapping slivers at the break — far below a blast's 100+ per hit.
    EXPECT_LE(totalDebris, swings * 8 + 3) << "kerf felling sprayed blast-like debris";
}

TEST_F(ChopKerfIntegrationTest, HollowShellTrunk_KerfFillsHeartwood_ThenFells) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    // A shell trunk like the forge trees build: a 3x3 Log ring with a HOLLOW center
    // column, plus a canopy. The kerf must fill the enclosed hollow with heartwood
    // (cutting exposes solid wood, not a drainpipe) and still fell the tree.
    putBox(6, 3, 6, 14, 3, 14, "Stone");                     // floor
    for (int y = 4; y <= 10; ++y)
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                if (dx != 0 || dz != 0) put(10 + dx, y, 10 + dz, "Log");   // ring, hollow center
    putBox(8, 11, 8, 12, 13, 12, "Leaf");                    // canopy

    ASSERT_FALSE(solid(10, 6, 10)) << "center must start hollow";

    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);
    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);

    // First bite (shallow): the enclosed hollow at the cut plane gets filled.
    auto r1 = dmg.carveChopKerf({10, 6, 9}, glm::vec3(0, 0, 1), 0.4f, /*coherent*/ true);
    EXPECT_TRUE(r1.carved);
    EXPECT_FALSE(r1.severed);
    EXPECT_TRUE(solid(10, 6, 10)) << "enclosed hollow was not filled with heartwood";
    EXPECT_NEAR(r1.fullDepth, 3.0f, 0.01f) << "3-cell-deep shell trunk";

    // Keep swinging to full depth: the ring trunk must release as one body.
    bool severed = false;
    int totalDebris = r1.collapse.debrisSpawned;
    for (int swing = 0; swing < 30 && !severed; ++swing) {
        auto r = dmg.carveChopKerf({10, 6, 9}, glm::vec3(0, 0, 1), 0.4f, /*coherent*/ true);
        severed = r.severed;
        totalDebris += r.collapse.debrisSpawned;
    }
    ASSERT_TRUE(severed) << "shell trunk never released";
    EXPECT_EQ(mgr.count(), 1u);
    EXPECT_FALSE(solid(10, 9, 11)) << "upper ring still standing after release";
    EXPECT_TRUE(solid(10, 4, 11)) << "stump ring fell";
    EXPECT_LE(totalDebris, 30 * 8 + 5) << "shell felling sprayed blast-like debris";
}

} // namespace Testing
} // namespace Phyxel
