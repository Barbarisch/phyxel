#include <gtest/gtest.h>

#include <set>

#include "core/SettlementLayout.h"
#include "core/TraversalProbe.h"

using namespace Phyxel::Core;

// ============================================================================
// Parcel fence (place_fence #22 / zone_parcel #21) — a building's plot is enclosed by a fence that a
// character CANNOT slip through anywhere except a gate. L2 = the fence covers the whole perimeter minus
// the gate; L3 = a character enters only through the gate (sealing the gate makes the yard unreachable).
// ============================================================================

namespace {
const AgentBox kAgent{2, 16, 4};

std::set<std::pair<int, int>> perimeterCells(const Rect& p) {
    std::set<std::pair<int, int>> s;
    for (int x = p.x; x < p.x1(); ++x) { s.insert({x, p.z}); s.insert({x, p.z1() - 1}); }
    for (int z = p.z; z < p.z1(); ++z) { s.insert({p.x, z}); s.insert({p.x1() - 1, z}); }
    return s;
}

// occupancy: ground below y=0; each fence cube cell solid 1 cube tall (9 micro) > step-up so it blocks.
struct FenceWorld {
    std::set<std::pair<int, int>> solid;   // fence cube cells
    bool occ(int x, int y, int z) const {
        if (y < 0) return true;                                   // ground (feet rest at 0)
        if (y >= 0 && y < 9 && solid.count({x / 9, z / 9})) return true;  // fence post (1 cube tall)
        return false;
    }
};
}  // namespace

// L2 — the fence covers the entire perimeter EXCEPT a gate of the requested width on the requested side;
// no other boundary cell is left open (no slip-through), and posts/gate don't overlap.
TEST(ParcelFenceTest, EnclosesPerimeterWithOnlyAGate) {
    const Rect parcel{2, 2, 10, 10};
    const int gateWidth = 2;
    const FencePlan f = planParcelFence(parcel, 'S', gateWidth);
    ASSERT_TRUE(f.ok);

    std::set<std::pair<int, int>> posts(f.posts.begin(), f.posts.end()), gate(f.gate.begin(), f.gate.end());
    EXPECT_EQ(posts.size(), f.posts.size()) << "duplicate post cells";
    EXPECT_EQ(gate.size(), static_cast<size_t>(gateWidth)) << "gate not the requested width";

    // gate cells are on the S side (z == parcel.z)
    for (const auto& g : gate) EXPECT_EQ(g.second, parcel.z) << "gate cell off the S side";

    // posts ∪ gate == full perimeter, and they don't overlap (so the ONLY opening is the gate)
    std::set<std::pair<int, int>> uni = posts;
    for (const auto& g : gate) { EXPECT_EQ(posts.count(g), 0u) << "gate cell also fenced"; uni.insert(g); }
    EXPECT_EQ(uni, perimeterCells(parcel)) << "fence+gate doesn't cover exactly the perimeter (a gap or spill)";
}

// L3 — a character outside walks IN through the gate; sealing the gate (fence it too) makes the yard
// unreachable, proving the fence is continuous (the gate is the only way in).
TEST(ParcelFenceTest, GateIsTheOnlyWayIn) {
    const Rect parcel{2, 2, 10, 10};
    const FencePlan f = planParcelFence(parcel, 'S', 2);
    ASSERT_TRUE(f.ok);

    const glm::ivec3 lo(-18, -2, -18), hi(20 * 9, 30, 20 * 9);
    // start just OUTSIDE the gate (one cube south of the S edge), goal = parcel interior centre.
    const int gx = (f.gate.front().first) * 9 + 4;
    const glm::ivec3 start(gx, 0, (parcel.z - 1) * 9 + 4);
    const int ix = (parcel.x + parcel.w / 2) * 9 + 4, iz = (parcel.z + parcel.d / 2) * 9 + 4;
    const glm::ivec3 gLo(ix - 2, -1, iz - 2), gHi(ix + 2, 1, iz + 2);

    FenceWorld open; open.solid.insert(f.posts.begin(), f.posts.end());
    TraversalProbe pOpen([&](int x, int y, int z) { return open.occ(x, y, z); }, kAgent);
    EXPECT_TRUE(pOpen.reachable(start, gLo, gHi, lo, hi)) << "can't enter the yard through the gate";

    // SEAL the gate: now the whole perimeter is fenced -> the yard is unreachable from outside.
    FenceWorld sealed = open;
    for (const auto& g : f.gate) sealed.solid.insert(g);
    TraversalProbe pSealed([&](int x, int y, int z) { return sealed.occ(x, y, z); }, kAgent);
    EXPECT_FALSE(pSealed.reachable(start, gLo, gHi, lo, hi)) << "reached a SEALED yard — the fence has a gap";
}

// A parcel too small to host a gate of the requested width on a side -> ok=false (graceful).
TEST(ParcelFenceTest, TooSmallForGateReportsNotOk) {
    EXPECT_FALSE(planParcelFence(Rect{0, 0, 2, 2}, 'S', 5).ok);
}
