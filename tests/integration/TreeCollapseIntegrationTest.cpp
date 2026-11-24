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

// ============================================================================
// Fix F6 — support flows through STRUCTURAL wood only (cube/subcube cross-
// section). Micro-only "twig" wood neither transmits support nor anchors.
// ============================================================================

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

// U2: the CrossSectionAnalyzer (DamageSystem::scorePlaneY) is the shared primitive behind
// chop neck-shear / statics / impact fracture. Verify its material-weighted area scoring:
// full cube = 9 subcube-units, static subcube = 1, microcubes = 0 (cargo), gated by weight.
TEST_F(TreeCollapseIntegrationTest, CrossSectionAnalyzer_ScoresMaterialWeightedArea) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";
    const int Y = 20;
    put(10, Y, 10, "Stone");                          // full Stone cube  -> 9 (stone), 0 (wood)
    put(12, Y, 10, "Log");                            // full Log cube    -> 9 (wood)
    // (the subcube / microcube cells went with the Chunk 1/9 m store, D3c-5)

    const glm::ivec3 center(13, Y, 10);
    auto woodW  = [](const std::string& s) { return s.rfind("Log", 0) == 0 ? 1.0f : 0.0f; };
    auto stoneW = [](const std::string& s) { return s == "Stone" ? 1.0f : 0.0f; };
    auto anyW   = [](const std::string&) { return 1.0f; };

    // Wood weight over x in [9,17]: the Log cube (9); Stone ignored.
    auto wood = DamageSystem::scorePlaneY(chunkManager.get(), center, 4, Y, woodW);
    EXPECT_FLOAT_EQ(wood.strength, 9.0f) << "wood cross-section = cube(9)";
    EXPECT_EQ(wood.cells.size(), 1u) << "one cell contributed wood (the Log cube)";

    // Stone weight: only the full Stone cube = 9.
    auto stone = DamageSystem::scorePlaneY(chunkManager.get(), center, 4, Y, stoneW);
    EXPECT_FLOAT_EQ(stone.strength, 9.0f);

    // Any-solid weight: Stone(9) + Log(9) = 18.
    auto all = DamageSystem::scorePlaneY(chunkManager.get(), center, 4, Y, anyW);
    EXPECT_FLOAT_EQ(all.strength, 18.0f);

    // A plane one above holds nothing -> zero strength, no cells.
    auto empty = DamageSystem::scorePlaneY(chunkManager.get(), center, 4, Y + 5, anyW);
    EXPECT_FLOAT_EQ(empty.strength, 0.0f);
    EXPECT_TRUE(empty.cells.empty());
}

} // namespace Testing
} // namespace Phyxel
