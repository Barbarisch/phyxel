#include <gtest/gtest.h>

#include <map>
#include <set>
#include <tuple>

#include "core/FenceBuilder.h"

using namespace Phyxel::Core;

// ============================================================================
// KI-5f — fences don't come to a neat corner (USER observation). The corner
// contract: each parcel corner has EXACTLY ONE full-height post, and the
// perpendicular run's rails/pickets REACH that post. The old composition
// shortened the W/E runs by a cube (avoiding a doubled post the endPosts
// mechanism already prevented), leaving an 8-9 micro rail/picket gap beside
// every corner post. This test materializes all four runs exactly the way the
// settlement stamper does and asserts both halves of the contract at all four
// corners; reverting the W/E spans to the old corner-excluded form fails the
// rail-reach assertions (mutation-sensitive).
// ============================================================================

namespace {
struct Cell { int x, y, z; bool operator<(const Cell& o) const {
    return std::tie(x, y, z) < std::tie(o.x, o.y, o.z); } };

// Mirror of the settlement stamper's run->world mapping (no gates/terrain).
std::set<Cell> materialize(int prX, int prZ, int prW, int prD,
                           int fH, int fSp, int fRails, FenceType type) {
    std::set<Cell> world;
    for (const auto& run : planParcelFenceRuns(prX, prZ, prW, prD)) {
        const int runLenMicro = run.toMicro - run.fromMicro;
        if (runLenMicro <= 0) continue;
        const FenceProfile prof =
            planFenceProfile(runLenMicro, fH, fSp, fRails, type, 1, run.cornerPosts);
        if (!prof.ok) continue;
        for (const auto& c : prof.cells) {
            const int wx = run.alongX ? run.fromMicro + c.u : run.fixedMicro + c.w;
            const int wz = run.alongX ? run.fixedMicro + c.w : run.fromMicro + c.u;
            world.insert({wx, c.y, wz});
        }
    }
    return world;
}

bool fullPostAt(const std::set<Cell>& w, int x, int z, int fH) {
    for (int y = 0; y < fH; ++y)
        if (!w.count({x, y, z})) return false;
    return true;
}
} // namespace

TEST(FenceCornerTest, EveryCornerHasOnePostAndRailsReachIt) {
    const int prX = 4, prZ = 6, prW = 8, prD = 6;
    const int fH = 12, fSp = 27, fRails = 2;
    const auto world = materialize(prX, prZ, prW, prD, fH, fSp, fRails, FenceType::Picket);
    ASSERT_FALSE(world.empty());

    // The four corner micro columns (each plane pair intersects at the run origin
    // micro of the boundary cubes, matching the stamper's w=0 mapping).
    struct Corner { int x, z; int dxIn, dzIn; };   // dIn: inward along each plane
    const Corner corners[4] = {
        {prX * 9,             prZ * 9,             +1, +1},   // SW
        {(prX + prW - 1) * 9, prZ * 9,             -1, +1},   // SE
        {prX * 9,             (prZ + prD - 1) * 9, +1, -1},   // NW
        {(prX + prW - 1) * 9, (prZ + prD - 1) * 9, -1, -1},   // NE
    };
    const int railY = fH * 1 / (fRails + 1);   // the first rail row

    // Post OWNERSHIP: count which runs contribute a full-height post at each corner
    // column. A deduping set can't see a doubled corner (both runs stamp the SAME
    // column — auditor-proven inert assert); counting contributors can.
    std::map<std::pair<int, int>, int> cornerPostWriters;
    for (const auto& run : planParcelFenceRuns(prX, prZ, prW, prD)) {
        const int runLenMicro = run.toMicro - run.fromMicro;
        const FenceProfile prof =
            planFenceProfile(runLenMicro, fH, fSp, fRails, FenceType::Picket, 1,
                             run.cornerPosts);
        // A run "writes a corner post" when a full-height column of its cells lands
        // on a corner micro column.
        std::map<int, int> colHeights;                        // u -> cell count at w=0
        for (const auto& cc : prof.cells)
            if (cc.w == 0) colHeights[cc.u]++;
        for (const auto& [u, cnt] : colHeights) {
            if (cnt < fH) continue;                           // not a full post column
            const int wx = run.alongX ? run.fromMicro + u : run.fixedMicro;
            const int wz = run.alongX ? run.fixedMicro : run.fromMicro + u;
            for (const auto& c : {std::pair<int,int>{prX * 9, prZ * 9},
                                  {(prX + prW - 1) * 9, prZ * 9},
                                  {prX * 9, (prZ + prD - 1) * 9},
                                  {(prX + prW - 1) * 9, (prZ + prD - 1) * 9}})
                if (wx == c.first && wz == c.second) cornerPostWriters[c]++;
        }
    }

    for (const auto& c : corners) {
        // (1) exactly ONE run owns the post at each corner column (counted per
        // contributing run — a doubled corner means two writers, invisible to the
        // deduped world set).
        EXPECT_TRUE(fullPostAt(world, c.x, c.z, fH))
            << "no corner post at (" << c.x << "," << c.z << ")";
        const int writers = cornerPostWriters[std::make_pair(c.x, c.z)];
        EXPECT_EQ(writers, 1)
            << "corner (" << c.x << "," << c.z << ") has " << writers
            << " post-writing runs (want exactly 1)";
        // (2) rails REACH the corner along BOTH planes: every micro step 1..8 from
        // the corner must carry the rail row (the old corner-excluded W/E spans left
        // exactly this band empty on the z-plane).
        for (int d = 1; d <= 8; ++d) {
            EXPECT_TRUE(world.count({c.x + c.dxIn * d, railY, c.z}))
                << "rail gap " << d << " micro from corner (" << c.x << "," << c.z
                << ") along x";
            EXPECT_TRUE(world.count({c.x, railY, c.z + c.dzIn * d}))
                << "rail gap " << d << " micro from corner (" << c.x << "," << c.z
                << ") along z (KI-5f: the old W/E spans stopped a cube short)";
        }
    }
}


// Ravenmere G-78 (2026-09-09): the tavern's east wall face was 1 cell from the next
// parcel's picket line - a 1.0 m alley, 0.67 m at the corner quoin, the only street->
// shrine route with 0.11 m of slack. A parcel plane that would leave < 2 clear cells
// against a neighbouring footprint is a pinch and must be dropped. RED on the old
// code: no such rule existed (every run was stamped).
TEST(FenceCornerTest, RunsPinchingANeighbourWallAreDropped) {
    using Phyxel::Core::CubeRect;
    using Phyxel::Core::fenceRunPinchesNeighbour;
    using Phyxel::Core::planParcelFenceRuns;
    // Parcel x 10..15, z 0..7. Its W plane is at x=10.
    const int prX = 10, prZ = 0, prW = 6, prD = 8;
    const auto runs = planParcelFenceRuns(prX, prZ, prW, prD);
    auto runOf = [&](char side) {
        for (const auto& r : runs) if (r.side == side) return r;
        ADD_FAILURE() << "no run for side " << side; return runs.front();
    };
    // A neighbour whose east wall cube is x=8: one clear cell (x=9) before the W plane.
    std::vector<CubeRect> oneCell  = {{2, 0, 7, 8}};
    // The same neighbour one cell further west: two clear cells (x=8, 9).
    std::vector<CubeRect> twoCells = {{1, 0, 7, 8}};
    EXPECT_TRUE (fenceRunPinchesNeighbour(runOf('W'), prX, prZ, prW, prD, oneCell,  2)) << "1-cell alley must pinch";
    EXPECT_FALSE(fenceRunPinchesNeighbour(runOf('W'), prX, prZ, prW, prD, twoCells, 2)) << "2-cell alley is a corridor";
    // The other planes do not face that neighbour.
    EXPECT_FALSE(fenceRunPinchesNeighbour(runOf('E'), prX, prZ, prW, prD, oneCell, 2));
    EXPECT_FALSE(fenceRunPinchesNeighbour(runOf('N'), prX, prZ, prW, prD, oneCell, 2));
    EXPECT_FALSE(fenceRunPinchesNeighbour(runOf('S'), prX, prZ, prW, prD, oneCell, 2));
    // No overlap along the run: a neighbour beside the plane but past the parcel's end.
    std::vector<CubeRect> pastTheEnd = {{2, 20, 7, 8}};
    EXPECT_FALSE(fenceRunPinchesNeighbour(runOf('W'), prX, prZ, prW, prD, pastTheEnd, 2));
    // N plane (an INNER plane, 8 micro inside the parcel edge at z=7): a neighbour whose
    // wall starts at z=9 leaves 1 cell + 8 micro = 17 micro < 18 -> pinch; at z=10 it is fine.
    std::vector<CubeRect> northClose = {{10, 9, 6, 5}};
    std::vector<CubeRect> northOk    = {{10, 10, 6, 5}};
    EXPECT_TRUE (fenceRunPinchesNeighbour(runOf('N'), prX, prZ, prW, prD, northClose, 2));
    EXPECT_FALSE(fenceRunPinchesNeighbour(runOf('N'), prX, prZ, prW, prD, northOk, 2));
    // A footprint INSIDE the parcel (its own building) is never a pinch for its own planes.
    std::vector<CubeRect> own = {{11, 1, 4, 5}};
    for (const auto& r : runs) EXPECT_FALSE(fenceRunPinchesNeighbour(r, prX, prZ, prW, prD, own, 2)) << r.side;
}


// Regen #7 after the pinch rule: the west plane was dropped but the south plane's corner
// post still stood on the pinch line beside the quoin (0.67 m). A surviving run must be
// pulled back by the alley width at every end whose corner plane was dropped, and it then
// owns its (moved) end posts.
TEST(FenceCornerTest, SurvivingRunsPullBackFromADroppedCorner) {
    using Phyxel::Core::planParcelFenceRuns;
    using Phyxel::Core::trimFenceRunAtDroppedCorners;
    const int prX = 10, prZ = 0, prW = 6, prD = 8;
    for (auto run : planParcelFenceRuns(prX, prZ, prW, prD)) {
        const int from0 = run.fromMicro, to0 = run.toMicro;
        const bool keep = trimFenceRunAtDroppedCorners(run, /*W*/true, false, false, false, 2);
        if (run.side == 'W') { continue; }               // the dropped plane itself is never stamped
        EXPECT_TRUE(keep) << run.side;
        if (run.side == 'S' || run.side == 'N') {
            EXPECT_EQ(run.fromMicro, from0 + 18) << run.side << " must start 2 cells east of the dropped W plane";
            EXPECT_EQ(run.toMicro, to0) << run.side << " far end untouched";
            EXPECT_TRUE(run.cornerPosts) << "a trimmed run posts its free end";
        } else {                                            // E plane: no corner with W
            EXPECT_EQ(run.fromMicro, from0); EXPECT_EQ(run.toMicro, to0);
        }
    }
    // Dropping both S and N leaves the W/E planes trimmed at both ends; a 2-cell-deep
    // parcel would have nothing left.
    for (auto run : planParcelFenceRuns(prX, prZ, prW, 4)) {
        if (run.alongX) continue;
        EXPECT_FALSE(trimFenceRunAtDroppedCorners(run, false, false, true, true, 2)) << run.side;
    }
}
