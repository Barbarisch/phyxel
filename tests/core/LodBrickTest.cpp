// C0 of docs/ContinuousLodPlan.md — the LOD squash operator.
//
// RED-BEFORE-GREEN: these tests are written against the NAIVE reduction rules
// (pure OR occupancy + volume-majority material) which the plan claims are
// wrong. TavernOpeningsSurviveL2 and SkinMaterialWinsOverCore are expected to
// FAIL first; ThinWallSurvives* pin the behaviour that must NOT regress while
// fixing them.
//
// Wall thicknesses are NOT invented — they are the set the structure generator
// actually emits (resources/structure_styles.json), in microcubes:
//   1 (timber_cottage interior, the thinnest thing the engine makes)
//   2 (timber_cottage exterior)   3 (stone_manor interior)
//   4 (timber_cottage foundation) 6 (stone_manor exterior)
//   9 = 1 cube (stone_keep interior)  27 = 3 cubes (stone_keep exterior)

#include <gtest/gtest.h>
#include "core/LodBrick.h"

using namespace Phyxel::Core;

namespace {

constexpr uint16_t kAir = 0;
constexpr uint16_t kStone = 1;
constexpr uint16_t kPlaster = 2;

// Coverage of a cube crossed by a wall `microThickness` microcubes thick:
// the wall spans a full 9x9 face of the cube and is N microcubes deep.
constexpr uint32_t coverageForWall(uint32_t microThickness) {
    return 9u * 9u * microThickness;   // 81 per microcube of thickness
}

LodCell solidCell(uint32_t coverage, uint16_t bulk, uint16_t skin) {
    LodCell c;
    c.coverage = coverage;
    c.bulkMaterial = bulk;
    c.skinMaterial = skin;
    return c;
}

// A wall in the X=const plane, `microThickness` thick, spanning the volume.
LodVolume makeWall(glm::ivec3 dim, int wallX, uint32_t microThickness,
                   uint16_t bulk = kStone, uint16_t skin = kStone) {
    LodVolume v(dim, 0);
    for (int y = 0; y < dim.y; ++y)
        for (int z = 0; z < dim.z; ++z)
            v.at(wallX, y, z) = solidCell(coverageForWall(microThickness), bulk, skin);
    return v;
}

} // namespace

// ---------------------------------------------------------------------------
// Determinism — required for persistence and for resume.
// ---------------------------------------------------------------------------
TEST(LodBrickTest, SquashIsDeterministic) {
    LodVolume v = makeWall({8, 8, 8}, 3, 3);
    v.at(5, 5, 5) = solidCell(LodVolume::kFullCoverage, kPlaster, kPlaster);

    SquashConfig cfg;
    LodVolume a = squash(v, cfg);
    LodVolume b = squash(v, cfg);

    ASSERT_EQ(a.cells().size(), b.cells().size());
    for (size_t i = 0; i < a.cells().size(); ++i) {
        EXPECT_EQ(a.cells()[i].coverage, b.cells()[i].coverage) << "cell " << i;
        EXPECT_EQ(a.cells()[i].bulkMaterial, b.cells()[i].bulkMaterial) << "cell " << i;
        EXPECT_EQ(a.cells()[i].skinMaterial, b.cells()[i].skinMaterial) << "cell " << i;
    }
}

TEST(LodBrickTest, SquashHalvesDimensionsAndRaisesLevel) {
    LodVolume v({8, 8, 8}, 0);
    LodVolume s = squash(v, SquashConfig{});
    EXPECT_EQ(s.dim(), glm::ivec3(4, 4, 4));
    EXPECT_EQ(s.level(), 1);
    EXPECT_EQ(s.cellSizeInCubes(), 2);
}

TEST(LodBrickTest, SquashRoundsOddDimensionsUp) {
    LodVolume v({7, 5, 3}, 0);
    LodVolume s = squash(v, SquashConfig{});
    EXPECT_EQ(s.dim(), glm::ivec3(4, 3, 2));
}

// ---------------------------------------------------------------------------
// Thin walls — every thickness the generator actually emits must survive.
// The rule that deletes the 1-microcube wall is the same rule that would let a
// character spawn inside geometry (see [[no-embedded-character-spawns]]).
// ---------------------------------------------------------------------------
TEST(LodBrickTest, ThinWallSurvivesAllAuthoredThicknesses_Or) {
    SquashConfig cfg;
    cfg.occupancy = OccupancyRule::Or;

    for (uint32_t micro : {1u, 2u, 3u, 4u, 6u, 9u, 27u}) {
        const uint32_t cov = std::min(coverageForWall(micro), LodVolume::kFullCoverage);
        LodVolume v({8, 8, 8}, 0);
        for (int y = 0; y < 8; ++y)
            for (int z = 0; z < 8; ++z)
                v.at(3, y, z) = solidCell(cov, kStone, kStone);

        // Squash all the way to a single cell; the wall must never vanish.
        auto levels = buildPyramid(v, cfg);
        for (size_t l = 1; l < levels.size(); ++l) {
            EXPECT_GT(levels[l].solidCellCount(), 0u)
                << micro << "-microcube wall vanished at LOD level " << l;
        }
    }
}

TEST(LodBrickTest, HalfThresholdDeletesTheThinnestAuthoredWall) {
    // Documents WHY the threshold rule is rejected: the 1-microcube
    // timber_cottage interior wall (structure_styles.json) is 81/729 = 11%
    // coverage, so a >=50% rule erases it at the very first level.
    SquashConfig cfg;
    cfg.occupancy = OccupancyRule::HalfThreshold;
    LodVolume v = makeWall({8, 8, 8}, 3, 1);
    ASSERT_GT(v.solidCellCount(), 0u);

    LodVolume s = squash(v, cfg);
    EXPECT_EQ(s.solidCellCount(), 0u)
        << "expected the threshold rule to delete a 1-microcube wall (this is why it is rejected)";
}

// ---------------------------------------------------------------------------
// Openings — a doorway must not be filled in by the occupancy merge.
// EXPECTED RED: OrPreserveOpenings currently behaves exactly like Or.
// ---------------------------------------------------------------------------
TEST(LodBrickTest, TavernOpeningsSurviveL2) {
    // A solid wall with a 2x2-cube doorway punched through it, marked as an
    // opening by the generator (the plan feeds these down from AssemblyPlan
    // rather than inferring them from voxels).
    SquashConfig cfg;
    cfg.occupancy = OccupancyRule::OrPreserveOpenings;

    LodVolume v = makeWall({8, 8, 8}, 3, 9);   // 1-cube-thick wall

    // A 1-cube doorway (~1 m, a real door width). CRITICAL: the opening must
    // SHARE a parent cell with solid wall, otherwise the test passes trivially
    // because the parent group contains no solid child at all. An earlier
    // version of this test used a 2x2 opening perfectly aligned to the 2x2x2
    // group and passed against the naive OR rule for exactly that reason.
    {
        LodCell& c = v.at(3, 0, 0);
        c = LodCell{};                          // air
        c.preserveOpening = true;
    }
    // Sanity: the parent group (1,0,0) really does contain solid wall siblings,
    // so OR has something to fill the hole in with.
    ASSERT_TRUE(v.at(3, 0, 1).solid());
    ASSERT_TRUE(v.at(3, 1, 0).solid());

    LodVolume s = squash(v, cfg);
    // Children (2..3, 0..1, 0..1) -> parent (1, 0, 0).
    const LodCell& p = s.at(1, 0, 0);
    EXPECT_TRUE(p.preserveOpening) << "opening flag must propagate upward";
    EXPECT_FALSE(p.solid())
        << "the doorway was filled in by the occupancy merge — a wall with no door";
}

// ---------------------------------------------------------------------------
// Material — a plaster-skinned stone wall must read as PLASTER at distance.
// EXPECTED RED: the surface-area vote is not implemented yet.
// ---------------------------------------------------------------------------
TEST(LodBrickTest, SkinMaterialWinsOverCore) {
    SquashConfig cfg;
    cfg.material = MaterialRule::SurfaceAreaMajority;

    // 2x2x2 group: a stone core cell plus skin cells whose exposed faces are
    // plaster. By VOLUME stone dominates; by exposed SURFACE plaster does.
    LodVolume v({2, 2, 2}, 0);
    v.at(0, 0, 0) = solidCell(LodVolume::kFullCoverage, kStone, kStone);
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z) {
                if (x == 0 && y == 0 && z == 0) continue;
                // thin plaster skin: little volume, lots of exposed surface
                v.at(x, y, z) = solidCell(coverageForWall(1), kStone, kPlaster);
            }

    LodVolume s = squash(v, cfg);
    const LodCell& p = s.at(0, 0, 0);
    EXPECT_EQ(p.skinMaterial, kPlaster)
        << "surface-area vote must pick the skin (plaster), not the core (stone)";
}

// ---------------------------------------------------------------------------
// Watertightness across a LOD boundary (plan §2.5) — the invariant whose
// absence killed the reverted Phase 5 coarse mesher.
// ---------------------------------------------------------------------------
TEST(LodBrickTest, AdjacentLevelsWatertight) {
    SquashConfig cfg;
    cfg.occupancy = OccupancyRule::Or;

    LodVolume fine = makeWall({8, 8, 8}, 3, 1);        // thinnest authored wall
    fine.at(6, 6, 6) = solidCell(LodVolume::kFullCoverage, kStone, kStone);

    LodVolume coarse = squash(fine, cfg);
    EXPECT_EQ(countWatertightViolations(fine, coarse), 0u)
        << "coarse level has holes where the fine level is solid — cracks at the LOD seam";
}

TEST(LodBrickTest, WatertightCheckActuallyDetectsAHole) {
    // The detector must be able to FAIL, or it proves nothing.
    SquashConfig cfg;
    cfg.occupancy = OccupancyRule::Or;
    LodVolume fine = makeWall({8, 8, 8}, 3, 1);
    LodVolume coarse = squash(fine, cfg);

    ASSERT_GT(coarse.solidCellCount(), 0u);
    // Punch a hole in the coarse level; violations must appear.
    for (int y = 0; y < coarse.dim().y; ++y)
        for (int z = 0; z < coarse.dim().z; ++z)
            coarse.at(1, y, z) = LodCell{};

    EXPECT_GT(countWatertightViolations(fine, coarse), 0u)
        << "watertight checker failed to notice a hole it was given";
}

// Openings vs watertightness are in DIRECT CONFLICT: carving a door makes a
// coarse cell empty where fine geometry is solid, which is exactly the shape of
// a crack. The resolution is that a deliberate opening is not a crack. This test
// pins that resolution so nobody "fixes" the checker back into conflict.
TEST(LodBrickTest, PreservedOpeningIsNotCountedAsACrack) {
    SquashConfig cfg;
    cfg.occupancy = OccupancyRule::OrPreserveOpenings;

    LodVolume fine = makeWall({8, 8, 8}, 3, 9);
    LodCell& door = fine.at(3, 0, 0);
    door = LodCell{};
    door.preserveOpening = true;

    LodVolume coarse = squash(fine, cfg);
    ASSERT_FALSE(coarse.at(1, 0, 0).solid()) << "precondition: the opening was carved";
    ASSERT_TRUE(coarse.at(1, 0, 0).preserveOpening);

    // The wall siblings inside that carved cell are fine-solid under a coarse
    // empty cell -- crack-shaped, but deliberate.
    EXPECT_EQ(countWatertightViolations(fine, coarse), 0u)
        << "a deliberate opening must not be reported as a crack";
}

// REGRESSION (solution-auditor, 2026-07-29): `parentFull` was a fixed
// kFullCoverage*8, which is only correct for a single squash from level 0.
// At deeper levels children already carry ACCUMULATED coverage, so a
// 12.5%-solid volume passed a ">= 50%" rule. Pins the level-scaled denominator.
TEST(LodBrickTest, HalfThresholdRejectsSparseVolumeAtDepth) {
    SquashConfig cfg;
    cfg.occupancy = OccupancyRule::HalfThreshold;

    // One fully-solid 2x2x2 octant inside a 4x4x4 volume => 8/64 = 12.5% solid.
    LodVolume v({4, 4, 4}, 0);
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z)
                v.at(x, y, z) = solidCell(LodVolume::kFullCoverage, kStone, kStone);

    LodVolume l1 = squash(v, cfg);      // level 1: that octant is 100% -> solid
    ASSERT_TRUE(l1.at(0, 0, 0).solid()) << "precondition: the full octant survives level 1";

    LodVolume l2 = squash(l1, cfg);     // level 2: only 1 of 8 children solid => 12.5%
    EXPECT_FALSE(l2.at(0, 0, 0).solid())
        << "a 12.5%-solid cell passed a >=50% threshold — parentFull is not level-scaled";
}

// The tie-break is documented ("ties break toward the lower palette index for
// determinism") but was previously unguarded: flipping argmax's `>` to `>=`
// broke ZERO tests. Determinism of the ORDER-SENSITIVE path needs its own test.
TEST(LodBrickTest, MaterialVoteTieBreaksToLowerPaletteIndex) {
    SquashConfig cfg;
    cfg.material = MaterialRule::SurfaceAreaMajority;

    // Two cells, equal exposure, different materials -> exact tie.
    LodVolume v({2, 2, 2}, 0);
    v.at(0, 0, 0) = solidCell(coverageForWall(1), kStone, kStone);
    v.at(1, 1, 1) = solidCell(coverageForWall(1), kPlaster, kPlaster);

    LodVolume s = squash(v, cfg);
    EXPECT_EQ(s.at(0, 0, 0).skinMaterial, kStone)
        << "tie must resolve to the LOWER palette index (kStone=1 < kPlaster=2)";
}

// Coverage bookkeeping: OR must never lose solid volume.
TEST(LodBrickTest, OrPreservesTotalCoverage) {
    SquashConfig cfg;
    cfg.occupancy = OccupancyRule::Or;
    LodVolume v = makeWall({8, 8, 8}, 3, 2);
    v.at(5, 5, 5) = solidCell(LodVolume::kFullCoverage, kStone, kStone);

    LodVolume s = squash(v, cfg);
    EXPECT_EQ(s.totalCoverage(), v.totalCoverage());
}

// ---------------------------------------------------------------------------
// SUB-BRICK OPENING MASK (OccupancyRule::OrWithOpeningMask).
//
// Why it exists, measured: the binary carve (OrPreserveOpenings) blanks a whole
// brick that contains any opening, which erased 49.7% of a settlement block's
// wall at 4^3 and 100% at 16^3 (docs/ContinuousLodPlan.md §2.3 sweep, fixtures
// B and C). The mask keeps the cell SOLID and carries the authored void volume
// upward as a conserved quantity for the renderer to act on.
// ---------------------------------------------------------------------------
namespace {
/// A 1-cube-thick wall in the X=wallX plane with one cube marked as a doorway,
/// the doorway carrying `openVol` microcubes of authored void.
LodVolume wallWithMaskedOpening(glm::ivec3 dim, int wallX, uint64_t openVol) {
    LodVolume v(dim, 0);
    for (int y = 0; y < dim.y; ++y)
        for (int z = 0; z < dim.z; ++z)
            v.at(wallX, y, z) = solidCell(LodVolume::kFullCoverage, kStone, kStone);
    LodCell& door = v.at(wallX, 0, 0);
    door.preserveOpening = true;
    door.openingCoverage = openVol;      // the void, recorded as a QUANTITY
    door.coverage = LodVolume::kFullCoverage - openVol;   // wall minus the carve
    return v;
}
} // namespace

TEST(LodBrickTest, OpeningMaskNeverDeletesGeometry) {
    SquashConfig mask;  mask.occupancy = OccupancyRule::OrWithOpeningMask;
    SquashConfig carve; carve.occupancy = OccupancyRule::OrPreserveOpenings;

    LodVolume v = wallWithMaskedOpening({8, 8, 8}, 3, 200);
    const size_t fineSolid = v.solidCellCount();
    ASSERT_GT(fineSolid, 0u);

    // The carve rule blanks the parent that contains the doorway...
    LodVolume carved = squash(v, carve);
    // ...the mask rule does not.
    LodVolume masked = squash(v, mask);

    EXPECT_GT(masked.solidCellCount(), carved.solidCellCount())
        << "the mask rule must retain geometry the binary carve deletes";
    EXPECT_FALSE(carved.at(1, 0, 0).solid()) << "precondition: carve blanks that parent";
    EXPECT_TRUE(masked.at(1, 0, 0).solid())  << "mask must keep the parent solid";
}

TEST(LodBrickTest, OpeningVolumeIsConservedThroughThePyramid) {
    SquashConfig mask; mask.occupancy = OccupancyRule::OrWithOpeningMask;
    LodVolume v = wallWithMaskedOpening({8, 8, 8}, 3, 200);
    // add a second opening so conservation isn't trivially one cell
    LodCell& w = v.at(3, 4, 4);
    w.preserveOpening = true; w.openingCoverage = 81;

    uint64_t expected = 0;
    for (const auto& c : v.cells()) expected += c.openingCoverage;
    ASSERT_EQ(expected, 281u);

    auto levels = buildPyramid(v, mask);
    for (size_t l = 1; l < levels.size(); ++l) {
        uint64_t total = 0;
        for (const auto& c : levels[l].cells()) total += c.openingCoverage;
        EXPECT_EQ(total, expected)
            << "opening volume must be CONSERVED at level " << l
            << " — the mask is worthless if coarsening loses it";
    }
}

TEST(LodBrickTest, OpeningMaskSurvivesToTheTopOfThePyramid) {
    SquashConfig mask; mask.occupancy = OccupancyRule::OrWithOpeningMask;
    LodVolume v = wallWithMaskedOpening({8, 8, 8}, 3, 200);
    auto levels = buildPyramid(v, mask);
    ASSERT_GE(levels.size(), 4u);
    const LodVolume& top = levels.back();
    uint64_t topOpen = 0;
    for (const auto& c : top.cells()) topOpen += c.openingCoverage;
    EXPECT_GT(topOpen, 0u) << "the coarsest level must still know an opening exists";
    EXPECT_GT(top.solidCellCount(), 0u) << "and must still have the building";
}

TEST(LodBrickTest, CoverageDoesNotTruncateAtDepth) {
    // coverage is uint64_t: a full level-8 cell holds 729*8^8 = 1.22e10 > UINT32_MAX.
    // A static_cast<uint32_t> in squash() used to truncate this silently.
    LodVolume v({2, 2, 2}, 7);
    const uint64_t fullAt7 = uint64_t(LodVolume::kFullCoverage) << (3u * 7);
    for (int x = 0; x < 2; ++x)
    for (int y = 0; y < 2; ++y)
    for (int z = 0; z < 2; ++z) {
        LodCell& c = v.at(x, y, z);
        c.coverage = fullAt7; c.bulkMaterial = c.skinMaterial = kStone;
    }
    SquashConfig cfg; cfg.occupancy = OccupancyRule::Or;
    LodVolume s = squash(v, cfg);
    EXPECT_EQ(s.at(0, 0, 0).coverage, fullAt7 * 8u)
        << "coverage truncated — 64-bit accumulation is required past level ~8";
    EXPECT_GT(s.at(0, 0, 0).coverage, uint64_t(0xFFFFFFFFu))
        << "test is only meaningful above the 32-bit ceiling";
}
