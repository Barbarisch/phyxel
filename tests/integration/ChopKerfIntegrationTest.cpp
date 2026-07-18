#include "IntegrationTestFixture.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "core/DamageSystem.h"
#include "core/CoherentFragmentManager.h"
#include "core/KinematicVoxelManager.h"
#include "core/MaterialRegistry.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelDynamicsWorld.h"
#include "scene/VoxelContactProbe.h"
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


TEST_F(ChopKerfIntegrationTest, DisconnectedNeckIsland_ShearsAndFells) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    // Live-reproduced failure (user chopped clean through, tree stood): a deep
    // notch fragments the cut plane into ISLANDS. The carve's connected flood
    // only sees the island under the blade — a DISCONNECTED sliver elsewhere on
    // the plane still carries support and is invisible to a
    // connectivity-limited neck shear. The shear must survey the whole plane.
    putBox(6, 3, 6, 16, 3, 14, "Stone");                 // floor
    putBox(10, 4, 10, 10, 6, 10, "Log");                  // column A (will be chopped)
    putBox(12, 4, 10, 12, 5, 10, "Log");                  // column B pillar
    Chunk* ch = chunkManager->getChunkAtCoord(ChunkManager::worldToChunkCoord({12, 6, 10}));
    ASSERT_NE(ch, nullptr);
    for (int sx = 0; sx < 3; ++sx)                        // B's neck: a 3-subcube SLIVER,
        ASSERT_TRUE(ch->addSubcube({12, 6, 10}, {sx, 0, 0}, "Log"));  // NOT connected to A's cell
    putBox(10, 7, 10, 12, 7, 10, "Log");                  // beam joins both columns up top
    putBox(9, 8, 9, 13, 9, 13, "Leaf");                   // canopy

    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);
    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);

    // Chop column A through. The gap at (11,6,10) keeps B's sliver island out of
    // A's connected cross-section — the box-survey shear must still count it,
    // snap it, and release the top.
    bool severed = false;
    for (int swing = 0; swing < 14 && !severed; ++swing) {
        auto r = dmg.carveChopKerf({10, 6, 10}, glm::vec3(0, 0, 1), 0.4f, /*coherent*/ true);
        severed = r.severed;
    }
    ASSERT_TRUE(severed) << "top stayed up: the disconnected neck island held it";
    EXPECT_EQ(mgr.count(), 1u) << "release was not one coherent body";
    EXPECT_FALSE(solid(11, 7, 10)) << "beam still standing after release";
    EXPECT_TRUE(solid(10, 4, 10)) << "column A stump fell";
    EXPECT_TRUE(solid(12, 4, 10)) << "column B pillar fell";
}

TEST_F(ChopKerfIntegrationTest, DeepBladeContact_SlotBitesAtTheBlade) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";

    // Live regression (micros-carved=0 whiffs): the bite window was capped at
    // kerfDepth from the FRONTIER, so a blade contact deeper than the frontier
    // (side-wall contact, stepped-in chop) produced an EMPTY window — the slot
    // removed nothing. Only the rim pocket fired, which never exposes heartwood.
    // The window must hug the blade wherever the blade actually is.
    putBox(6, 3, 6, 14, 3, 14, "Stone");     // floor
    putBox(10, 4, 9, 10, 12, 11, "Log");     // trunk 3 cells deep along z (9..11)
    putBox(8, 12, 8, 12, 15, 12, "Leaf");    // canopy

    DamageSystem dmg(chunkManager.get(), nullptr);
    // Blade contact 1.3 m past the front face (z=9): mid-trunk, cell (10,6,10).
    const glm::vec3 contact(10.5f, 6.5f, 10.3f);
    auto r = dmg.carveChopKerf({10, 6, 9}, glm::vec3(0, 0, 1), 0.36f,
                               /*coherent*/ false, contact);

    EXPECT_TRUE(r.carved) << "deep blade contact carved nothing";
    EXPECT_GT(r.microsRemoved, 0);
    EXPECT_GT(r.dHi, r.dLo) << "bite window is empty at a deep contact";
    // The SLOT reached the blade: cut faces at the blade's cell show heartwood
    // (the rim pocket alone never repaints — a pocket-only "bite" fails this).
    EXPECT_GT(countMicrosOf({10, 6, 10}, "LogHeartwood"), 0)
        << "slot never carved at the blade (pocket-only bite)";
}

TEST_F(ChopKerfIntegrationTest, DisjointWoodCell_ContactLandsOnSolidWood) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";

    // Auditor-prescribed case: a partially-chipped cell holds DISJOINT wood
    // fragments (routine after a few bites). The blade-contact point must lie
    // ON an actual solid fragment — a union-AABB clamp puts it in the air gap
    // BETWEEN fragments, and the bite window anchored there sits in air again
    // (the exact defect class this arc was fixing).
    const glm::ivec3 cell(10, 6, 10);
    Chunk* ch = chunkManager->getChunkAtCoord(ChunkManager::worldToChunkCoord(cell));
    ASSERT_NE(ch, nullptr);
    ASSERT_TRUE(ch->addSubcube(cell, {0, 0, 0}, "Log"));   // low corner fragment
    ASSERT_TRUE(ch->addSubcube(cell, {2, 2, 2}, "Log"));   // high corner fragment
    // (their union AABB spans the whole cell; the center third is pure air)

    // Blade just outside the cell's front face, aimed straight at the air gap.
    const glm::vec3 probe(10.5f, 6.5f, 9.9f);
    glm::vec3 onWood(0.0f);
    std::string mat;
    ASSERT_TRUE(DamageSystem::closestWoodPointInCell(chunkManager.get(), cell,
                                                     probe, onWood, &mat));
    EXPECT_EQ(mat.rfind("Log", 0), 0u);

    // The contact point must be on (within a hair of) one of the two fragments.
    auto onBox = [](const glm::vec3& p, const glm::vec3& lo, const glm::vec3& hi) {
        constexpr float e = 1e-4f;
        return p.x >= lo.x - e && p.x <= hi.x + e &&
               p.y >= lo.y - e && p.y <= hi.y + e &&
               p.z >= lo.z - e && p.z <= hi.z + e;
    };
    const glm::vec3 c0(cell);
    const bool onLow  = onBox(onWood, c0, c0 + glm::vec3(1.0f / 3.0f));
    const bool onHigh = onBox(onWood, c0 + glm::vec3(2.0f / 3.0f), c0 + glm::vec3(1.0f));
    EXPECT_TRUE(onLow || onHigh)
        << "contact point (" << onWood.x << "," << onWood.y << "," << onWood.z
        << ") is in the air gap between fragments, not on solid wood";
}

TEST_F(ChopKerfIntegrationTest, StationaryBladeAtTheFace_FellsAThinTrunk) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);
    buildSimpleTree();

    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);
    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);

    // The live loop: the blade lands ON the trunk face and the chopper does not
    // move. A 1-cube trunk is entirely within the blade-hugging window's reach,
    // so repeated identical swings must still cut through and fell the tree.
    const glm::vec3 contact(10.5f, 6.5f, 10.0f);   // on the front face
    bool severed = false;
    int swings = 0;
    for (int swing = 0; swing < 16 && !severed; ++swing) {
        auto r = dmg.carveChopKerf({10, 6, 10}, glm::vec3(0, 0, 1), 0.36f,
                                   /*coherent*/ true, contact);
        severed = r.severed;
        ++swings;
        EXPECT_TRUE(r.carved) << "swing " << swings << " removed nothing";
    }
    ASSERT_TRUE(severed) << "stationary-blade swings never cut a 1-cube trunk";
    EXPECT_GE(swings, 2) << "felling must take multiple swings";
    EXPECT_EQ(mgr.count(), 1u) << "release was not one coherent body";
}

TEST_F(ChopKerfIntegrationTest, SliverNeckAboveAnchorRow_TreeStillFalls) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    // Live user case (2026-07-16): the blade lands on the FAT ROOTED FLARE rows,
    // while the real neck — a few structural subcubes ONE ROW UP — is on a row
    // no swing ever anchors at. The old shear surveyed only the anchor row (fat,
    // way above threshold) and the release flood only re-evaluated components
    // touching the bite's rim (rooted flare stubs, anchored instantly) — so a
    // trunk visibly standing on ~5 slivers never fell.
    putBox(5, 3, 5, 15, 3, 15, "Stone");                    // floor
    putBox(8, 4, 8, 12, 5, 12, "Log");                      // fat rooted flare, y=4..5
    Chunk* ch = chunkManager->getChunkAtCoord(ChunkManager::worldToChunkCoord({10, 6, 10}));
    ASSERT_NE(ch, nullptr);
    for (int sx = 0; sx < 3; ++sx)                          // SLIVER NECK at y=6:
        ASSERT_TRUE(ch->addSubcube({10, 6, 10}, {sx, 0, 0}, "Log"));  // 3 subcubes
    putBox(10, 7, 10, 10, 10, 10, "Log");                   // upper trunk y=7..10
    putBox(9, 11, 9, 11, 12, 11, "Leaf");                   // canopy

    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);
    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);

    // Chop the FLARE (anchor row y=5, edge of the fat base) — the blade never
    // anchors at the y=6 sliver row. The band survey must still find the
    // 3-subcube neck (anchor.y+1) and release the top.
    bool severed = false;
    for (int swing = 0; swing < 10 && !severed; ++swing) {
        auto r = dmg.carveChopKerf({8, 5, 10}, glm::vec3(1, 0, 0), 0.4f, /*coherent*/ true,
                                   glm::vec3(8.0f, 5.5f, 10.5f));
        severed = r.severed;
    }
    ASSERT_TRUE(severed)
        << "top stayed up: the sliver neck above the anchor row was never surveyed";
    EXPECT_FALSE(solid(10, 9, 10)) << "upper trunk still standing after release";
    EXPECT_TRUE(solid(8, 4, 8)) << "rooted flare fell";
}

TEST_F(ChopKerfIntegrationTest, MicroOnlyNeck_ReleasesWithoutShear) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    // Variant: the neck is MICRO-ONLY wood (cargo — it must not hold a tree up,
    // F6). There is nothing structural to shear, so the release must come from
    // the flood re-evaluating the component ABOVE the cut — which the old code
    // only did when the bite's rim happened to touch it.
    putBox(5, 3, 5, 15, 3, 15, "Stone");                    // floor
    putBox(8, 4, 8, 12, 5, 12, "Log");                      // fat rooted flare
    Chunk* ch = chunkManager->getChunkAtCoord(ChunkManager::worldToChunkCoord({10, 6, 10}));
    ASSERT_NE(ch, nullptr);
    for (int mx = 0; mx < 3; ++mx)                          // micro-only neck: 3 micros
        ASSERT_TRUE(ch->addMicrocube({10, 6, 10}, {1, 0, 1}, {mx, 0, 1}, "Log"));
    putBox(10, 7, 10, 10, 10, 10, "Log");                   // upper trunk
    putBox(9, 11, 9, 11, 12, 11, "Leaf");                   // canopy

    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);
    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);

    bool severed = false;
    for (int swing = 0; swing < 10 && !severed; ++swing) {
        auto r = dmg.carveChopKerf({8, 5, 10}, glm::vec3(1, 0, 0), 0.4f, /*coherent*/ true,
                                   glm::vec3(8.0f, 5.5f, 10.5f));
        severed = r.severed;
    }
    ASSERT_TRUE(severed)
        << "top stayed up on a micro-only (cargo) neck — support re-evaluation missed it";
    EXPECT_FALSE(solid(10, 9, 10)) << "upper trunk still standing after release";
    EXPECT_TRUE(solid(8, 4, 8)) << "rooted flare fell";
}

TEST_F(ChopKerfIntegrationTest, OverhangTopSkin_DoesNotConductSupportAcrossAirGap) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    // Live user case #2 (2026-07-16, world-verified): the notch hollowed the
    // MIDDLE of the neck cells; the remaining subcubes there are the TOP-LAYER
    // (sy=2) underside skin of the overhanging mass above, and the only
    // physical bridge to the rooted stub is a cargo micro pillar. The support
    // flood conducts at CELL granularity, so "structural cell above structural
    // cell" counted as connected across a 2/3-cell AIR GAP — the tree hung on
    // an adjacency illusion, visibly standing on ~5 microcubes.
    putBox(5, 3, 5, 15, 3, 15, "Stone");                    // floor
    putBox(8, 4, 8, 12, 4, 12, "Log");                      // rooted flare, y=4
    Chunk* ch = chunkManager->getChunkAtCoord(ChunkManager::worldToChunkCoord({10, 5, 10}));
    ASSERT_NE(ch, nullptr);
    // Neck cells y=5: wood ONLY in the TOP subcube layer (sy=2) — the skin of
    // the mass above. >6 units total so the neck-shear cannot be what releases.
    for (int cx = 9; cx <= 11; ++cx)
        for (int sx = 0; sx < 3; ++sx)
            for (int sz = 0; sz < 3; sz += 2)
                ASSERT_TRUE(ch->addSubcube({cx, 5, 10}, {sx, 2, sz}, "Log"));   // 6 per cell, 18 total
    // The cargo micro pillar (the visible "support") — must hold nothing.
    for (int my = 0; my < 3; ++my)
        ASSERT_TRUE(ch->addMicrocube({10, 5, 10}, {1, 0, 1}, {1, my, 1}, "Log"));
    putBox(9, 6, 9, 11, 8, 11, "Log");                      // overhang mass + trunk
    putBox(8, 9, 8, 12, 11, 12, "Leaf");                    // canopy

    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);
    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);

    // Live follow-up (same fell): a micro-only CARGO cell adjacent to the falling
    // mass, whose standing wood below does NOT reach the shared face (partial stump
    // with no top-layer wood; cargo micros at the TOP of their cell) — cell-granular
    // stand-adjacency kept it STATIC, floating over the stump after the fell.
    // It must ride the fragment (or drop), never hover.
    for (int sy2 = 0; sy2 < 2; ++sy2)                       // partial stump (sy=0,1 only),
        ASSERT_TRUE(ch->addSubcube({13, 4, 10}, {1, sy2, 1}, "Log"));   // rooted via the flare
    for (int mx = 0; mx < 3; ++mx)                          // floater cargo at cell TOP
        ASSERT_TRUE(ch->addMicrocube({13, 5, 10}, {1, 2, 1}, {mx, 2, 1}, "Log"));
    put(12, 6, 10, "Log");                                  // horizontal bridge to the mass
    ASSERT_TRUE(ch->addSubcube({13, 6, 10}, {1, 0, 1}, "Log"));  // limb over the cargo

    // ONE bite at the rooted flare: the band re-seed evaluates the neighborhood,
    // and the vertical-contact rule must see that the top-skin cells have no
    // bottom-layer wood touching the flare — the whole overhang releases.
    bool severed = false;
    for (int swing = 0; swing < 6 && !severed; ++swing) {
        auto r = dmg.carveChopKerf({8, 4, 10}, glm::vec3(1, 0, 0), 0.4f, /*coherent*/ true,
                                   glm::vec3(8.0f, 4.5f, 10.5f));
        severed = r.severed;
    }
    ASSERT_TRUE(severed)
        << "overhang stayed up: cell-granular adjacency conducted support across the air gap";
    EXPECT_FALSE(solid(10, 7, 10)) << "overhang mass still standing after release";
    EXPECT_TRUE(solid(8, 4, 8)) << "rooted flare fell";
    // The floater cargo left the static grid (rode the fragment or dropped).
    EXPECT_EQ(countMicrosOf({13, 5, 10}, "Log"), 0)
        << "cargo micros left FLOATING over the stump after the fell";
    EXPECT_TRUE(solid(13, 4, 10)) << "the partial stump itself must stay rooted";
}

TEST_F(ChopKerfIntegrationTest, GroundedBranchTip_DoesNotAnchorTheTree) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    // Live user case #3 (2026-07-16): after a fell, a static GHOST THICKET of
    // canopy stayed standing in the world — the crown's branch tips drooped to
    // the ground, and the release flood's tree anchor ("a Log cell with terrain
    // directly below = rooted trunk") let a 1-subcube twig tip root the whole
    // cluster. The player then walked into invisible full-cell occupancy.
    // A ground-touching cell is a ROOT only with trunk-like wood meeting the
    // ground (full cube, or >=4 structural subcubes in the bottom layer —
    // real flare cells measure 5+, twig tips 1-2).
    putBox(6, 3, 6, 18, 3, 14, "Stone");                    // floor
    putBox(10, 4, 10, 10, 8, 10, "Log");                    // trunk y=4..8
    putBox(8, 9, 8, 13, 11, 12, "Leaf");                    // canopy
    Chunk* ch = chunkManager->getChunkAtCoord(ChunkManager::worldToChunkCoord({13, 4, 10}));
    ASSERT_NE(ch, nullptr);
    // Branch: cubes out from the trunk top, then a thin TWIG column drooping to
    // the ground (3 stacked subcubes per cell — F8-conducting, 1 sub per layer).
    // The twig sits OUTSIDE the 7x7 shear box around the cut (anchor.x±3 =
    // 7..13): inside it, the band shear snaps the twig row itself and masks the
    // anchor rule this test isolates.
    putBox(11, 8, 10, 14, 8, 10, "Log");                    // branch arm
    for (int y = 4; y <= 7; ++y)
        for (int sy2 = 0; sy2 < 3; ++sy2)
            ASSERT_TRUE(ch->addSubcube({14, y, 10}, {1, sy2, 1}, "Log"));  // twig column

    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);
    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);

    // Cut the trunk through at y=5. With the twig tip anchoring, the tree NEVER
    // falls (the whole cut-off crown "roots" through the drooping branch).
    // Contact steps IN with the notch (a real chopper walks forward); the twig
    // column also sits inside the 7x7 shear box inflating its count, so the cut
    // must complete via the slot — the release verdict is then purely the
    // anchor rule under test.
    bool severed = false;
    for (int swing = 0; swing < 16 && !severed; ++swing) {
        const glm::vec3 contact(10.5f, 5.5f, 10.0f + 0.25f * swing);
        auto r = dmg.carveChopKerf({10, 5, 10}, glm::vec3(0, 0, 1), 0.4f, /*coherent*/ true,
                                   contact);
        severed = r.severed;
    }
    ASSERT_TRUE(severed)
        << "tree never fell: a 1-subcube grounded twig tip anchored the crown";
    // Nothing of the crown/branch/twig stays as static world content.
    EXPECT_FALSE(solid(12, 8, 10)) << "branch arm left standing";
    EXPECT_FALSE(solid(14, 6, 10)) << "twig column left standing (the ghost-thicket bug)";
    EXPECT_FALSE(solid(10, 8, 10)) << "upper trunk left standing";
    EXPECT_TRUE(solid(10, 4, 10)) << "the real stump must stay";
}

TEST_F(ChopKerfIntegrationTest, FelledTree_ClearsCharacterCollision) {
    if (!isEnvironmentReady() || !chunkManager) GTEST_SKIP() << "env not ready";
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    // Live user question, verbatim: "when a tree falls, the old static portion
    // of it that is now dynamic/kinematic should be removed and the static
    // collision shape should be updated. why is it not?" — because the collapse
    // removal only reached the GPU-debris grid and the water sim; the per-chunk
    // VoxelOccupancyGrid the character capsule collides with was never rebuilt,
    // so the felled tree's whole static footprint kept blocking the player.
    buildSimpleTree();
    chunkManager->setPhysicsWorld(physicsWorld.get());
    Chunk* ch = chunkManager->getChunkAtCoord(ChunkManager::worldToChunkCoord({10, 6, 10}));
    ASSERT_NE(ch, nullptr);
    ch->setPhysicsWorld(physicsWorld.get());
    ch->createChunkPhysicsBody();
    ASSERT_GT(voxelWorld->gridCount(), 0u) << "probe rig broken: no occupancy grid registered";

    const glm::vec3 half(0.25f, 0.9f, 0.25f);   // the kinematic character capsule
    // Control: the STANDING trunk blocks the character probe.
    auto pre = Scene::sampleVoxelContact(*voxelWorld, {10.5f, 6.1f, 9.5f},
                                         {0, 0, 1}, half);
    ASSERT_TRUE(pre.forwardHit) << "probe rig broken: standing trunk not seen by the character";

    Core::KinematicVoxelManager   kin;
    Core::CoherentFragmentManager mgr;
    mgr.setDeps(voxelWorld, &kin);
    DamageSystem dmg(chunkManager.get(), nullptr);
    dmg.setFragmentManager(&mgr);
    bool severed = false;
    for (int swing = 0; swing < 16 && !severed; ++swing)
        severed = dmg.carveChopKerf({10, 6, 10}, glm::vec3(0, 0, 1), 0.35f,
                                    /*coherent*/ true).severed;
    ASSERT_TRUE(severed);
    ASSERT_FALSE(solid(10, 8, 10)) << "upper trunk still in the chunk data";

    // The felled trunk's static footprint must no longer block the character...
    auto post = Scene::sampleVoxelContact(*voxelWorld, {10.5f, 7.6f, 9.5f},
                                          {0, 0, 1}, half);
    EXPECT_FALSE(post.forwardHit)
        << "character still collides with the felled tree's ghost occupancy";
    // ...while the real stump keeps its collision.
    auto stump = Scene::sampleVoxelContact(*voxelWorld, {10.5f, 4.2f, 9.5f},
                                           {0, 0, 1}, half);
    EXPECT_TRUE(stump.forwardHit) << "stump lost its collision after the rebuild";
}

} // namespace Testing
} // namespace Phyxel
