#include <gtest/gtest.h>

#include "core/WaterOccupancy.h"

#include <cmath>
#include <limits>

// Water as occupancy (docs/WaterAsWorldData.md §5). The governing rule under test:
//   "water bodies should be defined by the terrain holding them, not the other way around"
// so every assertion here is about the span being a CONSEQUENCE of the column's real terrain.
//
// The defect this replaces: the far field drew water from a coarse 128 m bake with no reconciliation
// against per-voxel terrain — measured at 606 of 606 rim columns leaking, terrain up to 38 voxels
// below the water beside it, and water lying over a hillside.

namespace Phyxel {
namespace {

TEST(WaterOccupancyTest, SpanRestsExactlyOnTheTerrainSurface) {
    WaterSpan s;
    ASSERT_TRUE(buildOpenWaterSpan(/*surfaceY=*/119, /*bodyLevel=*/148.9f, s));
    EXPECT_EQ(s.bottomY, 120) << "water starts in the first cell ABOVE the solid voxel";
    EXPECT_FLOAT_EQ(s.topY, 148.9f) << "the surface is the body's level, not a rounded voxel";
    EXPECT_NEAR(s.depth(), 28.9f, 1e-4f);
    EXPECT_TRUE(s.valid());
}

TEST(WaterOccupancyTest, TerrainAboveTheLevelGetsNoWater) {
    // THE case that produced the reported artifact: a hillside standing above the lake surface had
    // water drawn over it because the coarse bake said the cell was wet. Terrain wins.
    WaterSpan s{};
    EXPECT_FALSE(buildOpenWaterSpan(/*surfaceY=*/160, /*bodyLevel=*/148.9f, s))
        << "ground above the waterline is dry land, whatever the bake claims";
    EXPECT_FALSE(buildOpenWaterSpan(/*surfaceY=*/148, /*bodyLevel=*/148.9f, s))
        << "solid voxel 148 occupies up to y=149, which is above the 148.9 surface";
}

TEST(WaterOccupancyTest, WaterCanNeverStartBelowTheGround) {
    // The structural invariant, swept rather than spot-checked: across a wide range of terrain
    // heights and levels, a returned span's bottom is ALWAYS above the solid terrain. There is no
    // input that makes this function emit floating water — that is the whole point of deriving the
    // bottom from surfaceY instead of accepting it as a parameter.
    int produced = 0;
    for (int surfaceY = -40; surfaceY <= 200; surfaceY += 7) {
        for (float level = -60.0f; level <= 260.0f; level += 3.5f) {
            WaterSpan s{};
            if (!buildOpenWaterSpan(surfaceY, level, s)) continue;
            ++produced;
            ASSERT_GT(s.bottomY, surfaceY)
                << "span bottom " << s.bottomY << " is inside/below terrain " << surfaceY;
            ASSERT_GT(s.topY, static_cast<float>(s.bottomY))
                << "a span must hold actual water (surfaceY=" << surfaceY << ", level=" << level << ")";
            ASSERT_GE(s.topY, static_cast<float>(surfaceY) + 1.0f)
                << "surface must stand above the ground it rests on";
        }
    }
    // ⚑WITHOUT THIS THE TEST IS VACUOUS. Every assertion above sits inside `if (!build) continue;`,
    // so an implementation that always returns false executes NO assertions and this test passes
    // while proving nothing (solution-auditor, 2026-08-03 — it stubbed the function and this stayed
    // green). Requiring the sweep to have actually produced spans is what gives it signal.
    EXPECT_GT(produced, 100) << "the sweep must actually exercise the water-producing path";
}

TEST(WaterOccupancyTest, ExtremeSurfaceYCannotOverflowIntoAFloatingSpan) {
    // ⚑A REAL COUNTEREXAMPLE TO THE HEADLINE CLAIM, found by sweeping the integer extremes.
    // `surfaceY + 1` is signed-int arithmetic: at INT_MAX it wraps to INT_MIN, and the function
    // returned TRUE with a span whose bottom sat ~4.3 billion units below its own terrain — water
    // untied to ground, exactly what this module exists to make impossible. Unreachable from real
    // terrain, but the invariant is stated absolutely, so it is tested absolutely.
    for (int surfaceY : {std::numeric_limits<int>::max(),
                         std::numeric_limits<int>::max() - 1,
                         std::numeric_limits<int>::min(),
                         std::numeric_limits<int>::min() + 1}) {
        WaterSpan s{};
        const float level = static_cast<float>(surfaceY) + 1000.0f;
        if (buildOpenWaterSpan(surfaceY, level, s)) {
            EXPECT_GT(s.bottomY, surfaceY)
                << "surfaceY=" << surfaceY << " produced bottomY=" << s.bottomY
                << " — containment broken by integer wraparound";
        }
    }
}

TEST(WaterOccupancyTest, AFilmThinnerThanTheHoldIsNotABody) {
    // Just barely over the ground is not water, it is z-fighting shimmer on the terrain.
    WaterSpan s{};
    EXPECT_FALSE(buildOpenWaterSpan(119, 120.0f + kMinSpanDepth * 0.5f, s));
    EXPECT_TRUE(buildOpenWaterSpan(119, 120.0f + kMinSpanDepth * 4.0f, s));
}

TEST(WaterOccupancyTest, NonFiniteLevelIsRejectedNotPropagated) {
    // A bad bake must not write NaN into world data, where every downstream consumer would inherit it.
    WaterSpan s{};
    EXPECT_FALSE(buildOpenWaterSpan(119, std::numeric_limits<float>::quiet_NaN(), s));
    EXPECT_FALSE(buildOpenWaterSpan(119, std::numeric_limits<float>::infinity(), s));
    EXPECT_FALSE(buildOpenWaterSpan(119, -std::numeric_limits<float>::infinity(), s));
}

TEST(WaterOccupancyTest, DeeperTerrainGivesDeeperWaterAtTheSameLevel) {
    // Depth is a consequence of the terrain, not a stored property of the body.
    WaterSpan shallow{}, deep{};
    ASSERT_TRUE(buildOpenWaterSpan(140, 148.9f, shallow));
    ASSERT_TRUE(buildOpenWaterSpan(100, 148.9f, deep));
    EXPECT_GT(deep.depth(), shallow.depth());
    EXPECT_FLOAT_EQ(deep.topY, shallow.topY) << "one body, one flat surface";
}

// ── EXTENT / CONNECTIVITY ─────────────────────────────────────────────────────────────────────
// The dominant defect: the bake decides a body's extent on a 128 m grid while the shoreline is a
// per-voxel contour, so ground sitting BELOW the waterline gets no water (606 of 606 rim columns).
// These pin the refinement — and, just as importantly, pin what must NOT be flooded.

namespace {
// A world where the bake calls a strip wet, and the terrain slopes so that reality extends further.
struct FakeWorld {
    std::function<int(int, int)> ground;
    std::function<float(int, int)> baked;
    ColumnTerrain terrain() const { return ColumnTerrain{ground, baked}; }
};
constexpr float kLevel = 100.0f;
}  // namespace

TEST(WaterOccupancyTest, SubmergedGroundBesideTheBodyJoinsIt) {
    // x <= 0 is baked wet. Ground is flat and well below the waterline everywhere, so columns just
    // outside the baked strip are plainly part of the same lake — the exact case the bake misses.
    FakeWorld w{[](int, int) { return 80; },
                [](int x, int) { return x <= 0 ? kLevel : kNoBody; }};
    EXPECT_FLOAT_EQ(connectedBodyLevel(5, 0, w.terrain(), 32), kLevel)
        << "ground 20 below the waterline, 5 columns from baked water, must be part of the body";
    EXPECT_FLOAT_EQ(connectedBodyLevel(0, 0, w.terrain(), 32), kLevel) << "a baked column is the body";
}

TEST(WaterOccupancyTest, GroundAboveTheWaterlineIsShoreNotWater) {
    // Same lake, but this column's ground stands above the surface. It is the bank.
    FakeWorld w{[](int x, int) { return x > 0 ? 120 : 80; },
                [](int x, int) { return x <= 0 ? kLevel : kNoBody; }};
    EXPECT_FLOAT_EQ(connectedBodyLevel(5, 0, w.terrain(), 32), kNoBody);
}

TEST(WaterOccupancyTest, ARidgeBlocksFloodingTheValleyBehindIt) {
    // ⚑THE TEST THAT STOPS THIS BECOMING A LEAK. A low valley sits beyond a ridge that breaks the
    // surface. It is below the waterline, but it is NOT connected — flooding it would put a lake on
    // the far side of a divide purely because the altitudes match. Naive "ground < level" does
    // exactly that; requiring a submerged PATH is what prevents it.
    FakeWorld w{[](int x, int) {
                    if (x <= 0) return 80;      // the lake bed
                    if (x <= 8) return 130;     // ridge, above the waterline
                    return 70;                  // valley beyond, below the waterline
                },
                [](int x, int) { return x <= 0 ? kLevel : kNoBody; }};
    EXPECT_FLOAT_EQ(connectedBodyLevel(20, 0, w.terrain(), 64), kNoBody)
        << "a submerged valley behind a ridge must NOT join the lake";
    EXPECT_FLOAT_EQ(connectedBodyLevel(3, 0, w.terrain(), 64), kNoBody)
        << "the ridge itself is above the surface";
}

TEST(WaterOccupancyTest, ConnectivityIsBoundedAndReportsHonestlyBeyondIt) {
    // Beyond maxSteps the search gives up and the column keeps the bake's answer. That is a real
    // limitation of a bounded search, not a silent success — pinned so nobody assumes global reach.
    FakeWorld w{[](int, int) { return 80; },
                [](int x, int) { return x <= 0 ? kLevel : kNoBody; }};
    EXPECT_FLOAT_EQ(connectedBodyLevel(4, 0, w.terrain(), 8), kLevel) << "inside the bound";
    EXPECT_FLOAT_EQ(connectedBodyLevel(400, 0, w.terrain(), 8), kNoBody) << "beyond it, unresolved";
}

TEST(WaterOccupancyTest, ConnectivityIsIndependentOfWhoAsks) {
    // Seam-freedom: the answer is a property of the world. Two columns either side of a notional
    // chunk border, at the same distance from the body, must agree — otherwise shorelines tear.
    FakeWorld w{[](int, int) { return 80; },
                [](int x, int) { return x <= 0 ? kLevel : kNoBody; }};
    const float a = connectedBodyLevel(7, 31, w.terrain(), 32);
    const float b = connectedBodyLevel(7, 32, w.terrain(), 32);   // across a 32-voxel chunk edge
    EXPECT_FLOAT_EQ(a, b);
    EXPECT_FLOAT_EQ(a, kLevel);
    // And repeated queries are stable.
    EXPECT_FLOAT_EQ(connectedBodyLevel(7, 31, w.terrain(), 32), a);
}

TEST(WaterOccupancyTest, ConnectivityHandlesMissingCallbacksAndZeroBudget) {
    ColumnTerrain empty{};
    EXPECT_FLOAT_EQ(connectedBodyLevel(0, 0, empty, 16), kNoBody) << "no callbacks: answer, do not crash";
    FakeWorld w{[](int, int) { return 80; },
                [](int x, int) { return x <= 0 ? kLevel : kNoBody; }};
    EXPECT_FLOAT_EQ(connectedBodyLevel(5, 0, w.terrain(), 0), kNoBody) << "zero budget reaches nothing";
}

// ── THE BASIN IS THE PRECONDITION ─────────────────────────────────────────────────────────────
// USER DIRECTIVE: "It should be impossible to just add water without a basin to put it in."
//
// So these tests do not hand the engine a water level and check what happens. They SHAPE TERRAIN
// first and then ask what it holds — which is the only question the API still accepts, because
// fillBasinAt has no level parameter to abuse.

namespace {
// Terrain built from a height function; no bake involved. The basin is the whole input.
ColumnTerrain shaped(std::function<int(int, int)> h) {
    ColumnTerrain t;
    t.groundY = std::move(h);
    t.bakedLevel = [](int, int) { return kNoBody; };
    return t;
}
}  // namespace

TEST(WaterOccupancyTest, FlatGroundHoldsNoWater) {
    // ⚑THE TEST THE OLD API COULD NOT FAIL. buildOpenWaterSpan(surfaceY=50, level=1000) happily
    // returns a 949-deep span on a flat plain — water resting on terrain that cannot hold it.
    // fillBasinAt cannot be asked that question: with no rim, the spill equals the ground and there
    // is no water. A plain is not a lake.
    BasinFill f{};
    EXPECT_FALSE(fillBasinAt(0, 0, shaped([](int, int) { return 50; }), 24, f));
    EXPECT_LT(f.depth(), kMinSpanDepth);
}

TEST(WaterOccupancyTest, ASlopeHoldsNoWater) {
    // Water runs downhill off an open slope; there is no containing rim in any direction.
    BasinFill f{};
    EXPECT_FALSE(fillBasinAt(0, 0, shaped([](int x, int) { return 50 + x; }), 24, f));
}

TEST(WaterOccupancyTest, ABasinFillsExactlyToItsRim) {
    // Dig a pit and the terrain decides the level: floor 40, rim 60 -> the water surface is the rim
    // (61 = the rim voxel's top face), never above it, and the depth follows from the terrain.
    auto pit = [](int x, int z) {
        const bool inPit = (x > -6 && x < 6 && z > -6 && z < 6);
        return inPit ? 40 : 60;
    };
    BasinFill f{};
    ASSERT_TRUE(fillBasinAt(0, 0, shaped(pit), 40, f)) << "a dug pit must hold water";
    EXPECT_FLOAT_EQ(f.groundY, 41.0f) << "seed ground is the pit floor's top face";
    EXPECT_FLOAT_EQ(f.level, 61.0f)   << "fills to the rim's top face, not one voxel more";
    EXPECT_NEAR(f.depth(), 20.0f, 1e-4f);
}

TEST(WaterOccupancyTest, ALowNotchInTheRimSetsTheLevelNotTheHighestWall) {
    // ⚑A basin fills to its LOWEST escape, not its tallest wall. Same pit, but one rim column is
    // cut down to 50: the water can only stand to 51 before it pours out through the notch. A model
    // that used the rim's average or maximum would over-fill and flood the surroundings.
    // ⚑The spillway must be a CHANNEL THAT LEADS OUT, not a single low column. A first version of
    // this fixture cut one notch voxel at (6,0) and left height-60 wall beyond it — so escaping
    // still meant climbing 60, and the fill correctly came out at 61. The test was wrong, not the
    // code. A basin's outlet is a path to the outside, not a dimple in the rim.
    auto notched = [](int x, int z) {
        const bool inPit = (x > -6 && x < 6 && z > -6 && z < 6);
        if (inPit) return 40;
        if (z == 0 && x >= 6) return 50;      // a spillway channel running away from the pit
        return 60;
    };
    BasinFill f{};
    ASSERT_TRUE(fillBasinAt(0, 0, shaped(notched), 40, f));
    EXPECT_FLOAT_EQ(f.level, 51.0f) << "the lowest rim decides the level";
    EXPECT_NEAR(f.depth(), 10.0f, 1e-4f);
}

TEST(WaterOccupancyTest, DeeperBasinHoldsMoreWaterAtTheSameRim) {
    auto pit = [](int depth) {
        return [depth](int x, int z) {
            const bool inPit = (x > -6 && x < 6 && z > -6 && z < 6);
            return inPit ? depth : 60;
        };
    };
    BasinFill shallow{}, deep{};
    ASSERT_TRUE(fillBasinAt(0, 0, shaped(pit(55)), 40, shallow));
    ASSERT_TRUE(fillBasinAt(0, 0, shaped(pit(20)), 40, deep));
    EXPECT_FLOAT_EQ(shallow.level, deep.level) << "same rim, same surface";
    EXPECT_GT(deep.depth(), shallow.depth())   << "a deeper bowl holds more";
}

TEST(WaterOccupancyTest, BasinFillIsIndependentOfWhichColumnAsks) {
    // Any column in the same bowl must agree on the surface, or one lake would render at two
    // heights. Seeds at the centre and near the wall must return the same level.
    auto pit = [](int x, int z) {
        const bool inPit = (x > -6 && x < 6 && z > -6 && z < 6);
        return inPit ? 40 : 60;
    };
    BasinFill a{}, b{};
    ASSERT_TRUE(fillBasinAt(0, 0, shaped(pit), 40, a));
    ASSERT_TRUE(fillBasinAt(4, -4, shaped(pit), 40, b));
    EXPECT_FLOAT_EQ(a.level, b.level) << "one basin, one surface";
}

// ── BATCH FLOOD ──────────────────────────────────────────────────────────────────────────────
//
// The batch pass exists purely to make the per-column query affordable, so the test that matters is
// not "does it produce plausible output" but "does it produce THE SAME output". A faster function
// that quietly disagrees with the one it replaces is worse than the slow one.

namespace {

// Build the two grids the batch pass consumes from a pair of world-space functions, over a block
// whose origin is (ox, oz). Keeps the tests reading in world terms like the per-column ones.
struct Grid {
    int w = 0, d = 0, ox = 0, oz = 0;
    std::vector<float> ground, baked, level;
    void build(int ox_, int oz_, int w_, int d_,
               const std::function<int(int, int)>& g, const std::function<float(int, int)>& b,
               int maxSteps = 16) {
        ox = ox_; oz = oz_; w = w_; d = d_;
        ground.resize(static_cast<size_t>(w) * d);
        baked.resize(ground.size());
        level.resize(ground.size());
        for (int z = 0; z < d; ++z)
            for (int x = 0; x < w; ++x) {
                const size_t i = static_cast<size_t>(z) * w + x;
                ground[i] = static_cast<float>(g(ox + x, oz + z)) + 1.0f;   // top face
                baked[i]  = b(ox + x, oz + z);
            }
        floodBodiesOverGrid(w, d, ground.data(), baked.data(), level.data(), maxSteps);
    }
    float at(int wx, int wz) const {
        const int x = wx - ox, z = wz - oz;
        if (x < 0 || z < 0 || x >= w || z >= d) return kNoBody;
        return level[static_cast<size_t>(z) * w + x];
    }
};

}  // namespace

TEST(WaterOccupancyTest, BatchFloodIsIndependentOfTheWindowItWasComputedIn) {
    // ⚑THE SEAM TEST, and the reason the step bound exists. A chunk resolves its water in a padded
    // window; the neighbouring chunk resolves the SAME shoreline columns in a different window. If
    // the answer depends on the window, the two disagree and the shoreline tears at the border.
    //
    // RED ON THE REAL DEFECT: the first version of floodBodiesOverGrid had no step bound, so it
    // spread as far as the grid reached. The far seed below is beyond the budget but inside the
    // large window — unbounded, the large window floods from it and the small one cannot, and the
    // two disagree. Bounded, both correctly ignore it.
    // ⚑THE FIXTURE HAS TO BE BUILT SO THE TWO WINDOWS SEE DIFFERENT BODIES, or the test proves
    // nothing. A first version put both bodies at the same level and the mutation did NOT go red:
    // whichever body won, the answer was 16 either way, so the disagreement was invisible. Here the
    // far body sits at a DIFFERENT level and is the nearest seed to the probed column, so a flood
    // that over-reaches produces a visibly different number rather than a coincidentally equal one.
    auto ground = [](int, int) { return 10; };              // low everywhere: nothing blocks by terrain
    auto baked  = [](int x, int) {
        if (x >= -10 && x <= -8) return 16.0f;               // NEAR body
        if (x >= -60 && x <= -58) return 22.0f;              // FAR body, at a different surface
        return kNoBody;
    };

    // The probed column sits 42 steps from the far body and 92 from the near one — beyond the
    // 16-step budget of both, so the correct answer is DRY regardless of which window asks.
    Grid narrow, wide;
    narrow.build(-116, -16, 33, 33, ground, baked, 16);      // pads the probe by 16; sees NEITHER body
    wide.build(-200, -60, 400, 120, ground, baked, 16);      // sees both

    EXPECT_FLOAT_EQ(narrow.at(-100, 0), wide.at(-100, 0))
        << "window-dependent answer at (-100,0) — chunk borders would tear here";
    EXPECT_FLOAT_EQ(wide.at(-100, 0), kNoBody) << "42 steps from any body: dry";

    // Non-vacuity: the same fixture must actually produce water where a body IS in reach, or the
    // agreement above is just two functions returning nothing.
    EXPECT_FLOAT_EQ(wide.at(0, 0), 16.0f) << "8 steps from the near body: wet, at ITS level";
}

TEST(WaterOccupancyTest, BatchFloodRespectsItsStepBudget) {
    // Low ground everywhere and one seed, in a grid far larger than the budget. Columns inside the
    // budget flood; columns beyond it must not, however much room the flood is given.
    auto ground = [](int, int) { return 5; };
    auto baked  = [](int x, int) { return (x == -60) ? 16.0f : kNoBody; };
    Grid g;
    g.build(-120, -20, 240, 40, ground, baked, 20);
    EXPECT_FLOAT_EQ(g.at(-45, 0), 16.0f)   << "15 steps from the seed: within budget";
    EXPECT_FLOAT_EQ(g.at(-40, 0), 16.0f)   << "20 steps from the seed: exactly at budget";
    EXPECT_FLOAT_EQ(g.at(-38, 0), kNoBody) << "22 steps: beyond budget, must stay dry";
}

TEST(WaterOccupancyTest, BatchFloodStopsAtARidgeLikeTheSingleColumnPathDoes) {
    // The rule that stops a lake swallowing the next valley must survive the rewrite. Wet strip at
    // x < -6, a wall at x == 0 standing above the waterline, and a deep trench beyond it.
    auto ground = [](int x, int) { return (x == 0) ? 40 : 10; };
    auto baked  = [](int x, int) { return (x < -6) ? 16.0f : kNoBody; };
    Grid g;
    g.build(-20, -20, 40, 40, ground, baked);

    EXPECT_GT(g.at(-3, 0), kNoBody * 0.5f) << "submerged ground on the wet side joins the body";
    EXPECT_FLOAT_EQ(g.at(0, 0), kNoBody)   << "the ridge itself breaks the surface";
    EXPECT_FLOAT_EQ(g.at(5, 0), kNoBody)   << "the trench beyond the ridge must NOT flood";
}

TEST(WaterOccupancyTest, BatchFloodGivesEachBodyItsOwnSurface) {
    // Two bodies at different levels, separated by a divide. Columns near each must take THEIR
    // body's surface — a single global level would render one lake at the other's height.
    auto ground = [](int x, int) { return (x > -3 && x < 3) ? 90 : 10; };
    auto baked  = [](int x, int) {
        if (x <= -10) return 20.0f;
        if (x >=  10) return 50.0f;
        return kNoBody;
    };
    Grid g;
    g.build(-30, -30, 60, 60, ground, baked);
    EXPECT_FLOAT_EQ(g.at(-5, 0), 20.0f) << "left column takes the left body's level";
    EXPECT_FLOAT_EQ(g.at( 5, 0), 50.0f) << "right column takes the right body's level";
    EXPECT_FLOAT_EQ(g.at( 0, 0), kNoBody) << "the divide holds neither";
}

TEST(WaterOccupancyTest, BatchFloodLeavesADryWorldDry) {
    // No bake anywhere means no seeds, so nothing may become wet however low the terrain sits.
    // This is the batch-side statement of "water needs a source"; a flood that invents its own
    // seeds would drown a world with no water in it.
    auto ground = [](int, int) { return 5; };
    auto baked  = [](int, int) { return kNoBody; };
    Grid g;
    g.build(0, 0, 16, 16, ground, baked);
    for (float v : g.level) EXPECT_FLOAT_EQ(v, kNoBody);
}

TEST(WaterOccupancyTest, BatchFloodHandlesDegenerateGrids) {
    // Null pointers and non-positive extents must return rather than walk off memory — the caller
    // sizes these grids from chunk arithmetic, and a zero-size region is a legal thing to ask for.
    std::vector<float> one{5.0f}, lvl{0.0f};
    EXPECT_NO_FATAL_FAILURE(floodBodiesOverGrid(0, 0, one.data(), one.data(), lvl.data(), 8));
    EXPECT_NO_FATAL_FAILURE(floodBodiesOverGrid(1, 1, nullptr, one.data(), lvl.data(), 8));
    EXPECT_NO_FATAL_FAILURE(floodBodiesOverGrid(-4, 3, one.data(), one.data(), lvl.data(), 8));
    EXPECT_NO_FATAL_FAILURE(floodBodiesOverGrid(1, 1, one.data(), one.data(), lvl.data(), -1));
    EXPECT_FLOAT_EQ(lvl[0], 0.0f) << "a rejected call must not write output";
}

}  // namespace
}  // namespace Phyxel
