#include "IntegrationTestFixture.h"
#include "core/ChunkManager.h"
#include "core/DamageSystem.h"
#include "core/MaterialRegistry.h"
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

} // namespace Testing
} // namespace Phyxel
