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
 * Tree-object felling (docs/DestructionSystemV2.md Phase 2). A tree must fell when its
 * TRUNK is cut, even though its canopy brushes terrain / its trunk is flanked by a
 * slope: a tree is rooted ONLY through its trunk to the ground, so incidental
 * leaf-or-side terrain contact must NOT anchor a severed top. Built headless with
 * materialed voxels so the flood logic can be iterated without an engine reboot.
 *
 * These are the cases that FAIL on the pre-tree-object baseline (the severed top stays
 * anchored via terrain contact) and must pass once tree-object anchoring lands.
 */
class TreeCollapseIntegrationTest : public ChunkManagerTestFixture {
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
    /// Add one microcube (1/9 voxel) into the cell at world pos, at subcube slot `sub`
    /// (0..2 per axis) / micro slot `mic` (0..2 per axis).
    bool putMicro(glm::ivec3 cell, glm::ivec3 sub, glm::ivec3 mic, const char* mat) {
        Chunk* ch = chunkManager->getChunkAtCoord(ChunkManager::worldToChunkCoord(cell));
        if (!ch) return false;
        return ch->addMicrocube(ChunkManager::worldToLocalCoord(cell), sub, mic, mat);
    }
    int countSolid(int x0, int y0, int z0, int x1, int y1, int z1) {
        int n = 0;
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z) if (solid(x, y, z)) ++n;
        return n;
    }
};

TEST_F(TreeCollapseIntegrationTest, OverhangCanopy_CutTrunk_TopDetaches) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";

    // A rooted tree with a canopy that touches a terrain "hill" (Stone at x14, against
    // the canopy's x13 face). supportY=3 anchors the floor + hill base.
    putBox(6, 3, 6, 14, 3, 14, "Stone");    // floor
    putBox(10, 4, 10, 10, 12, 10, "Log");   // 1x1 trunk, y4..12
    putBox(7, 12, 7, 13, 16, 13, "Leaf");   // canopy
    putBox(14, 3, 7, 14, 16, 13, "Stone");  // hill wall flush against the canopy (x13<->x14)

    const int canopy0[3] = {7, 12, 7}, canopy1[3] = {13, 16, 13};
    ASSERT_GT(countSolid(7, 12, 7, 13, 16, 13), 200) << "canopy setup failed";

    DamageSystem dmg(chunkManager.get(), nullptr);
    // Cut the 1x1 trunk at y8 (removes y7..9), severing the top from the stump.
    dmg.applyDamage(glm::vec3(10.5f, 8.5f, 10.5f), 1.5f, 500.0f, "force",
                    glm::vec3(0.0f), /*supportY*/ 3.0f, /*collapse*/ true, /*coherent*/ false);

    // The severed top (canopy) must fall — leaf-terrain contact must not anchor it.
    EXPECT_LT(countSolid(canopy0[0], canopy0[1], canopy0[2], canopy1[0], canopy1[1], canopy1[2]), 20)
        << "canopy stayed up — still anchored via leaf<->hill contact (tree-object rule missing)";
    // The stump (below the cut), the floor, and the hill must all stay.
    EXPECT_TRUE(solid(10, 5, 10)) << "stump trunk fell (should stay, rooted to floor)";
    EXPECT_TRUE(solid(10, 3, 10)) << "floor fell";
    EXPECT_TRUE(solid(14, 10, 10)) << "hill fell";
}

TEST_F(TreeCollapseIntegrationTest, EmbeddedTrunk_CutTrunk_TopDetaches) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";

    // The upper trunk is FLANKED by a rising hillside (Stone columns at x9 and x11, right
    // against the trunk at x10). Cutting the trunk must still fell the top — side terrain
    // contact is not a root (only a Log with terrain directly below roots a tree).
    putBox(6, 3, 6, 14, 3, 14, "Stone");    // floor (supportY anchor)
    putBox(10, 4, 10, 10, 12, 10, "Log");   // trunk
    putBox(7, 12, 7, 13, 14, 13, "Leaf");   // canopy
    putBox(9, 3, 10, 9, 12, 10, "Stone");   // hillside flanking the trunk (-x)
    putBox(11, 3, 10, 11, 12, 10, "Stone"); // hillside flanking the trunk (+x)

    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.applyDamage(glm::vec3(10.5f, 8.5f, 10.5f), 1.5f, 500.0f, "force",
                    glm::vec3(0.0f), /*supportY*/ 3.0f, /*collapse*/ true, /*coherent*/ false);

    EXPECT_FALSE(solid(10, 11, 10)) << "top trunk stayed — anchored via the flanking hillside";
    EXPECT_LT(countSolid(7, 12, 7, 13, 14, 13), 20) << "canopy stayed up";
    EXPECT_TRUE(solid(10, 5, 10)) << "stump fell (should stay, rooted)";
    EXPECT_TRUE(solid(9, 10, 10)) << "flanking hill fell";
}

TEST_F(TreeCollapseIntegrationTest, StandingTree_AdjacentTerrainBlast_TreeStays) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";

    // A rooted tree with a terrain pillar FLUSH against the trunk. Blasting the pillar
    // high up (y=7, well above yAnchor=3, so there is no yAnchor short-circuit) makes the
    // collapse flood actually REACH the tree from the pillar rim. The tree must NOT
    // over-detach — it stays up via the rooted-trunk anchor (a Log with terrain below).
    // (If the rooted-trunk check were removed, the tree's pure-tree flood would find no
    // anchor and the whole tree would wrongly fall — so this genuinely guards it.)
    putBox(6, 3, 6, 14, 3, 14, "Stone");    // floor (supportY=3 anchor)
    putBox(10, 4, 10, 10, 12, 10, "Log");   // trunk
    putBox(7, 12, 7, 13, 14, 13, "Leaf");   // canopy
    putBox(11, 4, 10, 11, 10, 10, "Stone"); // terrain pillar flush against the trunk (x11 vs x10)

    DamageSystem dmg(chunkManager.get(), nullptr);
    // radius 1.0: removes the pillar cell at (11,7,10) (which borders the trunk, so the
    // trunk is seeded) but the trunk at (10,7,10) is at distance 1.0 → falloff 0 → NOT
    // broken. So the flood reaches an INTACT tree and must decide to keep it.
    dmg.applyDamage(glm::vec3(11.5f, 7.5f, 10.5f), 1.0f, 500.0f, "force",
                    glm::vec3(0.0f), /*supportY*/ 3.0f, /*collapse*/ true, /*coherent*/ false);

    EXPECT_TRUE(solid(10, 7, 10)) << "trunk at blast height missing (should be grazed, not broken)";
    EXPECT_TRUE(solid(10, 9, 10)) << "trunk over-detached (flood reached the tree and dropped it)";
    // Canopy box is 7*3*7 = 147 cells; a fully-standing tree keeps ~all of them, a
    // wrongly-detached one drops to near 0 — so >140 is a clean over-detach guard.
    EXPECT_GT(countSolid(7, 12, 7, 13, 14, 13), 140) << "canopy over-detached";
}

// ============================================================================
// P2.1 — microcube-resolution trees + "leaves shed, wood topples"
// ============================================================================

TEST_F(TreeCollapseIntegrationTest, MicroLeafCanopy_OverhangCutTrunk_TopDetaches) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";

    // Like OverhangCanopy, but the canopy is MICROCUBE-only Leaf cells (real fine trees).
    // Red on baseline: cellMaterial() reads only cube/first-subcube, so a micro-only leaf
    // cell reports "" -> NOT a tree cell -> the flood spreads canopy->hill -> anchored ->
    // the severed top never falls.
    putBox(6, 3, 6, 14, 3, 14, "Stone");    // floor (yAnchor 3)
    putBox(10, 4, 10, 10, 12, 10, "Log");   // 1x1 trunk
    // Micro-leaf canopy: cells (8..12, 13..15, 8..12), a few Leaf micros per cell.
    for (int x = 8; x <= 12; ++x)
        for (int y = 13; y <= 15; ++y)
            for (int z = 8; z <= 12; ++z) {
                ASSERT_TRUE(putMicro({x, y, z}, {1, 1, 1}, {1, 1, 1}, "Leaf"));
                ASSERT_TRUE(putMicro({x, y, z}, {1, 0, 1}, {1, 1, 1}, "Leaf"));
            }
    // Canopy must connect to the trunk top: trunk cube (10,12,10) borders cell (10,13,10). ✓
    ASSERT_TRUE(solid(10, 13, 10)) << "micro cells not registered in hasVoxelAt";
    putBox(13, 3, 8, 13, 15, 12, "Stone");  // hill wall flush against the canopy (x12<->x13)

    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.applyDamage(glm::vec3(10.5f, 8.5f, 10.5f), 1.5f, 500.0f, "force",
                    glm::vec3(0.0f), /*supportY*/ 3.0f, /*collapse*/ true, /*coherent*/ false);

    EXPECT_LT(countSolid(8, 13, 8, 12, 15, 12), 10)
        << "micro-leaf canopy stayed up — micro cells not recognized as tree material";
    EXPECT_TRUE(solid(10, 5, 10)) << "stump fell";
    EXPECT_TRUE(solid(13, 10, 10)) << "hill fell";
}

TEST_F(TreeCollapseIntegrationTest, MicroWoodCoheres_CanopyRidesAlong) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    // A floating (unrooted -> detaches when seeded) tree piece: 2 Log cubes + one cell of
    // 4 Log MICROcubes (a fine branch) + 2 Leaf cubes (canopy). With coherent=true the
    // WOOD (cubes + micros, full micro resolution) topples as ONE body and the CANOPY
    // RIDES ALONG as cargo (F1: leaves are never voxel debris — the tree falls as a tree).
    putBox(20, 20, 20, 21, 20, 20, "Log");   // 2 Log cubes
    for (int m = 0; m < 4; ++m)
        ASSERT_TRUE(putMicro({22, 20, 20}, {0, 1, 1}, {m % 3, m / 3, 1}, "Log"));  // 4 Log micros
    putBox(20, 21, 20, 21, 21, 20, "Leaf");  // 2 Leaf cubes (canopy)

    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);
    const size_t bodiesBefore = voxelWorld->getBodyCount();

    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);
    // Blast the first Log cube only (r=1.0: neighbours at dist 1.0 -> falloff 0, survive).
    auto damageRes = dmg.applyDamage(glm::vec3(20.5f, 20.5f, 20.5f), 1.0f, 600.0f, "force",
                    glm::vec3(0.0f), DamageSystem::NO_SUPPORT, /*collapse*/ true, /*coherent*/ true);

    ASSERT_EQ(mgr.count(), 1u) << "severed micro-wood did not cohere (micro gather missing)";
    EXPECT_EQ(voxelWorld->getBodyCount(), bodiesBefore + 1);
    // The fragment holds the wood (1 remaining Log cube + 4 Log micros = 5) AND the
    // canopy cargo: leaf cell (21,21,20) is nearest the detached wood -> rides; leaf
    // (20,21,20) sat over the destroyed cube -> orphan (removed, no debris). So the
    // fragment has 5 wood + at least 1 leaf voxel.
    ASSERT_EQ(kin.getObjects().size(), 1u);
    const auto& frag = kin.getObjects().begin()->second;
    int wood = 0, leaf = 0;
    for (const auto& v : frag.voxels) {
        if (v.materialName.rfind("Leaf", 0) == 0) ++leaf; else ++wood;
    }
    EXPECT_EQ(wood, 5) << "wood voxels missing from the fragment";
    EXPECT_GE(leaf, 1) << "canopy did not ride the fragment (leaves must not shed/vanish)";
    // Everything is gone from the grid (wood + riding leaves into the body; orphan
    // leaves removed without debris).
    EXPECT_EQ(countSolid(20, 20, 20, 22, 21, 20), 0);
    // Leaves never become voxel debris: total debris stays small (only the one blasted
    // Log cube's pieces + the body), far below what leaf-shed would produce.
    EXPECT_LE(damageRes.debrisSpawned, 30);
}

// ============================================================================
// Fix F1 (post-demo) — support flows through WOOD only; leaves are cargo
// ============================================================================

TEST_F(TreeCollapseIntegrationTest, InterlockedCanopies_CutTrunk_TopStillFalls) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";

    // Two trees whose LEAF canopies touch (the demo's floating-birch case). Tree A is
    // rooted; tree B's trunk is cut. Support must flow through WOOD only — B's severed
    // top must NOT stay up via the leaf bridge into A. And the leaf cargo split must
    // not strip A: A keeps its wood and (nearly all of) its canopy.
    putBox(4, 3, 4, 18, 3, 16, "Stone");     // floor (yAnchor 3)
    putBox(8, 4, 10, 8, 10, 10, "Log");      // tree A trunk (rooted)
    putBox(6, 11, 8, 10, 13, 12, "Leaf");    // tree A canopy: x6..10
    putBox(13, 4, 10, 13, 10, 10, "Log");    // tree B trunk (rooted)
    putBox(11, 11, 8, 15, 13, 12, "Leaf");   // tree B canopy: x11..15 (touches A at x10|x11)

    DamageSystem dmg(chunkManager.get(), nullptr);
    // Cut B's trunk at y7 (r=1.0: removes only (13,7,10); neighbours survive).
    dmg.applyDamage(glm::vec3(13.5f, 7.5f, 10.5f), 1.0f, 600.0f, "force",
                    glm::vec3(0.0f), /*supportY*/ 3.0f, /*collapse*/ true, /*coherent*/ false);

    // B's severed top: upper trunk + its canopy must FALL (not float via the leaf bridge).
    EXPECT_FALSE(solid(13, 9, 10)) << "B's upper trunk floats — leaf bridge still transmits support";
    EXPECT_LT(countSolid(12, 11, 8, 15, 13, 12), 8)
        << "B's canopy stayed up (should fall with its wood)";
    // A must be untouched where it counts: trunk intact, canopy substantially intact.
    EXPECT_TRUE(solid(8, 9, 10))  << "A's trunk was damaged";
    EXPECT_TRUE(solid(13, 5, 10)) << "B's stump (below the cut, rooted) fell";
    // A's canopy is 5x3x5=75 cells; the cargo split may cost a boundary sliver at most.
    EXPECT_GT(countSolid(6, 11, 8, 10, 13, 12), 55)
        << "A's canopy was stripped by B's fall (cargo split too greedy)";
}

// ============================================================================
// Fix F2 (post-demo) — bounded collision boxes for big fragments
// ============================================================================

TEST_F(TreeCollapseIntegrationTest, LongMicroBranch_FellsWithBoundedCollisionBoxes) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);
    ASSERT_EQ(voxelWorld->getBodyCount(), 0u);

    // A floating micro-resolution branch spanning 10 cells (90 fine cells > the 64 merge
    // grid). On the pre-F2 code the merge falls back to ONE COLLISION BOX PER VOXEL —
    // the 2005-box pine that tanked the demo to 4 FPS. The fell must produce a body
    // with a BOUNDED box count (cell-resolution collision), while the render keeps
    // every fine voxel.
    for (int x = 20; x <= 29; ++x)
        for (int m = 0; m < 4; ++m)
            ASSERT_TRUE(putMicro({x, 25, 25}, {1, 1, 1}, {m % 3, m / 3, 1}, "Log"));

    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);
    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);
    // Blast the first cell only; the remaining 9 cells (36 micro voxels) detach.
    dmg.applyDamage(glm::vec3(20.5f, 25.5f, 25.5f), 1.0f, 600.0f, "force",
                    glm::vec3(0.0f), DamageSystem::NO_SUPPORT, /*collapse*/ true, /*coherent*/ true);

    ASSERT_EQ(mgr.count(), 1u) << "branch did not cohere";
    auto* body = voxelWorld->getBodyById(1);
    ASSERT_NE(body, nullptr);
    EXPECT_LE(body->getLocalBoxes().size(), 12u)
        << "collision box count unbounded (per-voxel fallback — the 2005-box pine bug)";
    // Render fidelity preserved: all 36 fine voxels in the kinematic object.
    ASSERT_EQ(kin.getObjects().size(), 1u);
    EXPECT_EQ(kin.getObjects().begin()->second.voxels.size(), 36u);
}

// ============================================================================
// P2.3 — directional hinge topple (asymmetry decides the fall direction)
// ============================================================================

TEST_F(TreeCollapseIntegrationTest, AsymmetricTop_TipsTowardItsHeavySide) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);
    ASSERT_EQ(voxelWorld->getBodyCount(), 0u);   // fresh world -> first body id is 1

    // A rooted trunk whose severed top carries a heavy Log arm toward +X. When the
    // trunk is cut, the top's COM sits +X of the cut pivot, so the piece must be
    // seeded to TIP toward +X: rotation about up×(+x) = -z, COM velocity toward +X.
    putBox(6, 3, 6, 14, 3, 14, "Stone");     // floor
    putBox(10, 4, 10, 10, 10, 10, "Log");    // 1x1 trunk, y4..10
    putBox(11, 10, 10, 14, 10, 10, "Log");   // heavy arm toward +X at the top

    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);
    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);
    // Cut the trunk at y7 (r=1.0 removes only that cube; neighbours survive).
    dmg.applyDamage(glm::vec3(10.5f, 7.5f, 10.5f), 1.0f, 600.0f, "force",
                    glm::vec3(0.0f), /*supportY*/ 3.0f, /*collapse*/ true, /*coherent*/ true);

    ASSERT_EQ(mgr.count(), 1u) << "severed top did not cohere";
    auto* body = voxelWorld->getBodyById(1);
    ASSERT_NE(body, nullptr);
    // Hinge seed: tipping toward +X = rotation about -Z (up × +x), COM moving +X.
    EXPECT_LT(body->angularVelocity.z, -0.05f)
        << "no hinge rotation toward the heavy side (angVel.z should be negative)";
    EXPECT_GT(body->linearVelocity.x, 0.0f) << "COM not moving toward the heavy side";
    // The tip axis is dominated by the asymmetry direction (about Z, not X).
    EXPECT_GT(std::abs(body->angularVelocity.z), std::abs(body->angularVelocity.x));
}

TEST_F(TreeCollapseIntegrationTest, SymmetricTop_TipsAlongChopDirection) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);
    ASSERT_EQ(voxelWorld->getBodyCount(), 0u);

    // A symmetric top (plain vertical trunk) has no mass asymmetry — the fall
    // direction must come from the CHOP direction (+Z here, the damage dir bias).
    putBox(6, 3, 6, 14, 3, 14, "Stone");
    putBox(10, 4, 10, 10, 12, 10, "Log");

    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);
    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);
    dmg.applyDamage(glm::vec3(10.5f, 7.5f, 10.5f), 1.0f, 600.0f, "force",
                    glm::vec3(0.0f, 0.0f, 1.0f),   // chop direction: +Z
                    /*supportY*/ 3.0f, /*collapse*/ true, /*coherent*/ true);

    ASSERT_EQ(mgr.count(), 1u) << "severed top did not cohere";
    auto* body = voxelWorld->getBodyById(1);
    ASSERT_NE(body, nullptr);
    // Tipping toward +Z = rotation about up × z = +X, COM moving +Z.
    EXPECT_GT(body->angularVelocity.x, 0.05f)
        << "no hinge rotation along the chop direction (angVel.x should be positive)";
    EXPECT_GT(body->linearVelocity.z, 0.0f) << "COM not moving along the chop direction";
}

// ============================================================================
// Fix F5 (post-demo) — MIXED log+leaf cells must be wood-floodable
// ============================================================================

TEST_F(TreeCollapseIntegrationTest, MixedLogLeafCells_WholeCanopyFalls) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    // Forge trees MIX micro-Log branch wood and micro-Leaf foliage in the SAME cell.
    // First-sub-voxel classification labels a leaf-first mixed cell "leaf", the wood
    // flood skips it, and every branch beyond it is unreachable — left FLOATING when
    // the trunk below is severed (the live DestructionDemo floating-canopy bug:
    // only 80+76 of a full birch detached; a blast probe on the "ghost" canopy broke
    // 340 real voxels). A cell containing ANY wood must transmit the wood flood.
    putBox(6, 3, 6, 14, 3, 14, "Stone");     // floor (yAnchor 3)
    putBox(10, 4, 10, 10, 7, 10, "Log");     // rooted trunk, y4..7
    // BARRIER mixed cell at (10,8,10): Leaf micro in the LOWEST-ordered subcube slot
    // (found first by a first-voxel scan), Log micro deeper in the cell.
    ASSERT_TRUE(putMicro({10, 8, 10}, {0, 0, 0}, {0, 0, 0}, "Leaf"));
    ASSERT_TRUE(putMicro({10, 8, 10}, {1, 1, 1}, {1, 1, 1}, "Log"));
    putBox(10, 9, 10, 10, 10, 10, "Log");    // upper branch wood, reachable ONLY through the mixed cell
    putBox(9, 10, 9, 11, 11, 11, "Leaf");    // canopy around the upper wood

    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);
    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);
    // Sever the trunk at y5 (r=1.0 removes only that cube; neighbours survive).
    dmg.applyDamage(glm::vec3(10.5f, 5.5f, 10.5f), 1.0f, 600.0f, "force",
                    glm::vec3(0.0f), /*supportY*/ 3.0f, /*collapse*/ true, /*coherent*/ true);

    // The WHOLE top must go: trunk above the cut, the mixed cell, the upper wood, and
    // the canopy — nothing floats.
    EXPECT_FALSE(solid(10, 6, 10)) << "trunk above the cut floats";
    EXPECT_FALSE(solid(10, 8, 10)) << "mixed log+leaf cell floats (classified leaf, skipped by wood flood)";
    EXPECT_FALSE(solid(10, 9, 10)) << "upper wood floats — unreachable past the mixed cell";
    EXPECT_EQ(countSolid(9, 8, 9, 11, 11, 11), 0)
        << "canopy left floating (the DestructionDemo ghost-canopy bug)";
    // And it fell as coherent pieces, not a shower. The twig-scale mixed cell can't
    // rigidly bind the two wood chunks (F6: micro wood transmits no support), so the
    // trunk piece and the upper piece fall as TWO bodies — the twig and canopy riding
    // their nearest wood.
    ASSERT_EQ(mgr.count(), 2u) << "severed pieces did not cohere";
    ASSERT_EQ(kin.getObjects().size(), 2u);
    int wood = 0, leaf = 0;
    for (const auto& [id, obj] : kin.getObjects()) {
        for (const auto& v : obj.voxels) {
            if (v.materialName.rfind("Leaf", 0) == 0) ++leaf; else ++wood;
        }
    }
    // Wood: trunk y6..7 (2) + the mixed cell's Log micro (1) + upper wood y9..10 (2).
    // Leaves: 17 canopy cubes (3x2x3 region minus the Log cell) + the mixed cell's
    // Leaf micro = 18 — the mixed cell's own foliage rides along.
    EXPECT_EQ(wood, 5) << "wood missing from the fragments";
    EXPECT_EQ(leaf, 18) << "canopy did not ride the fragments";
}

// ============================================================================
// Fix F6 — support flows through STRUCTURAL wood only (cube/subcube cross-
// section). Micro-only "twig" wood neither transmits support nor anchors.
// ============================================================================

TEST_F(TreeCollapseIntegrationTest, GroundTouchingTwigSkirt_DoesNotAnchor) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    // The live pine: after a base cut, a drooping micro-wood skirt (twig cells reaching
    // the ground) kept the whole tree "rooted" — an ~11cm twig column held a multi-ton
    // trunk up. Twig wood must not anchor.
    putBox(6, 3, 6, 14, 3, 14, "Stone");     // floor (yAnchor 3)
    putBox(10, 4, 10, 10, 10, 10, "Log");    // rooted 1x1 trunk, y4..10
    // Micro-wood skirt: a twig chain from the trunk at y6 down to a ground-touching
    // cell at (11,4,10) (terrain directly below -> a rooted anchor under pre-F6 rules).
    for (int y = 4; y <= 6; ++y)
        for (int m = 0; m < 4; ++m)
            ASSERT_TRUE(putMicro({11, y, 10}, {1, 1, 1}, {m % 3, m / 3, 1}, "LogPine"));

    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);
    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);
    // Sever the trunk at y5 (r=1.0 removes only that cube; the skirt cells survive).
    dmg.applyDamage(glm::vec3(10.5f, 5.5f, 10.5f), 1.0f, 600.0f, "force",
                    glm::vec3(0.0f), /*supportY*/ 3.0f, /*collapse*/ true, /*coherent*/ true);

    // The top must FALL: the surviving skirt (micro wood touching the ground) must not
    // hold it up.
    EXPECT_FALSE(solid(10, 6, 10)) << "trunk floats — twig skirt anchored the tree";
    EXPECT_FALSE(solid(10, 8, 10)) << "upper trunk floats";
    EXPECT_EQ(countSolid(10, 6, 10, 10, 10, 10), 0) << "severed top stayed up";
    EXPECT_GE(mgr.count(), 1u) << "severed top did not cohere";
    // The rooted stump below the cut stays.
    EXPECT_TRUE(solid(10, 4, 10)) << "rooted stump fell";
}

TEST_F(TreeCollapseIntegrationTest, TwigBridgeBetweenTrees_DoesNotTransmitSupport) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    // The live birch: interpenetrating canopies touch twig-to-twig. Cutting tree B's
    // trunk left it hanging off tree A's branches through micro-wood contact. Twig
    // wood must not transmit support between trees.
    putBox(4, 3, 4, 18, 3, 16, "Stone");     // floor (yAnchor 3)
    putBox(8, 4, 10, 8, 10, 10, "Log");      // tree A trunk (rooted)
    putBox(13, 4, 10, 13, 10, 10, "Log");    // tree B trunk (rooted)
    // Twig bridge: micro-wood cells linking A's top to B's top (x9..12 at y10).
    for (int x = 9; x <= 12; ++x)
        for (int m = 0; m < 4; ++m)
            ASSERT_TRUE(putMicro({x, 10, 10}, {1, 1, 1}, {m % 3, m / 3, 1}, "Log"));

    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);
    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);
    // Cut B's trunk at y6 (r=1.0 removes only that cube).
    dmg.applyDamage(glm::vec3(13.5f, 6.5f, 10.5f), 1.0f, 600.0f, "force",
                    glm::vec3(0.0f), /*supportY*/ 3.0f, /*collapse*/ true, /*coherent*/ true);

    // B's severed top must FALL — the twig bridge into rooted A must not hold it.
    EXPECT_FALSE(solid(13, 8, 10)) << "B's top floats — twig bridge transmits support";
    EXPECT_EQ(countSolid(13, 7, 10, 13, 10, 10), 0) << "B's severed top stayed up";
    // A is untouched: trunk intact top to bottom.
    EXPECT_TRUE(solid(8, 10, 10)) << "A's trunk top was taken down";
    EXPECT_TRUE(solid(8, 4, 10))  << "A's trunk base was taken down";
    // B's rooted stump below the cut stays.
    EXPECT_TRUE(solid(13, 4, 10)) << "B's rooted stump fell";
}

// ============================================================================
// Fix F7 — support never flows between different tree SPECIES. Structural
// limb-to-limb contact between two trees is touching, not attachment.
// ============================================================================

TEST_F(TreeCollapseIntegrationTest, CrossSpeciesLimbContact_DoesNotTransmitSupport) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    // The live birch: after a full base cut it stayed up because its structural limbs
    // touch the oak's structural limbs (the flood walked 269 cells into the oak's
    // rooted trunk). Cube/subcube-grade wood of DIFFERENT species is never one
    // organism — contact must not transmit support.
    putBox(4, 3, 4, 18, 3, 16, "Stone");        // floor (yAnchor 3)
    putBox(8, 4, 10, 8, 10, 10, "Log");         // oak trunk (rooted)
    putBox(9, 10, 10, 10, 10, 10, "Log");       // oak limb toward +X (structural)
    putBox(13, 4, 10, 13, 10, 10, "LogBirch");  // birch trunk (rooted)
    putBox(11, 10, 10, 12, 10, 10, "LogBirch"); // birch limb toward -X — TOUCHES the oak
                                                // limb cube-to-cube at x10|x11
    // Cut the birch trunk at y6 (r=1.0 removes only that cube).
    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);
    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);
    dmg.applyDamage(glm::vec3(13.5f, 6.5f, 10.5f), 1.0f, 600.0f, "force",
                    glm::vec3(0.0f), /*supportY*/ 3.0f, /*collapse*/ true, /*coherent*/ true);

    // The birch's severed top must FALL — resting on the oak's limb is not support.
    EXPECT_FALSE(solid(13, 8, 10)) << "birch top floats — cross-species limb contact transmits support";
    EXPECT_EQ(countSolid(13, 7, 10, 13, 10, 10), 0) << "birch severed top stayed up";
    EXPECT_EQ(countSolid(11, 10, 10, 12, 10, 10), 0) << "birch limb stayed up";
    // The oak is untouched: trunk + limb intact.
    EXPECT_TRUE(solid(8, 10, 10))  << "oak trunk was taken down";
    EXPECT_TRUE(solid(10, 10, 10)) << "oak limb was taken down";
    // The birch's rooted stump below the cut stays.
    EXPECT_TRUE(solid(13, 4, 10)) << "birch rooted stump fell";
}

// U0/F1: a blast on a MICRO-only leaf cell must emit ZERO voxel debris — leaves are
// foliage, never voxels. Pre-fix the blast scan read subcubes only (getStaticSubcubesAt),
// so a micro-resolution leaf cell fell through to a "Wood" representative material and
// scattered wood voxel debris: the "fireballed leaves explode into blocks" report.
TEST_F(TreeCollapseIntegrationTest, BlastOnMicroLeafCell_EmitsNoVoxelDebris) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";

    // A cell containing ONLY Leaf microcubes (no cube, no subcube) — real fine-tree foliage.
    const glm::ivec3 cell(10, 10, 10);
    for (int mx = 0; mx < 3; ++mx)
        for (int my = 0; my < 3; ++my)
            for (int mz = 0; mz < 3; ++mz)
                putMicro(cell, glm::ivec3(1, 1, 1), glm::ivec3(mx, my, mz), "LeafBirch");
    ASSERT_TRUE(solid(10, 10, 10)) << "micro leaf cell not created";

    DamageSystem dmg(chunkManager.get(), nullptr);   // no GPU: spawnDebris no-ops, counter still tallies
    auto res = dmg.applyDamage(glm::vec3(10.5f, 10.5f, 10.5f), 1.5f, 1500.0f, "force",
                               glm::vec3(0.0f), Phyxel::DamageSystem::NO_SUPPORT,
                               /*collapse*/ false, /*coherentFragments*/ false);

    EXPECT_GT(res.voxelsBroken, 0) << "blast never reached the leaf cell (test setup wrong)";
    EXPECT_EQ(res.debrisSpawned, 0)
        << "leaf foliage spawned voxel debris — F1 violated (the fireball-explodes-leaves bug)";
}

} // namespace Testing
} // namespace Phyxel
