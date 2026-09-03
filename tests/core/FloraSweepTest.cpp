#include <gtest/gtest.h>

#include <set>

#include "core/FloraSweep.h"

using namespace Phyxel::Core;

// ============================================================================
// Orphaned-canopy sweep (user find 2026-08-27: floating tree pieces after a
// settlement build). Site prep clears flora inside its own band; a tree whose
// TRUNK stood in that band keeps everything that reached outside it.
//
// The rule under test: tree matter that cannot reach support THROUGH tree
// matter is orphaned — EXCEPT when its component leaves the scan box, where
// support is simply unknown and must never be guessed away.
// RED baseline: planOrphanedFloraSweep returns {}.
// ============================================================================

namespace {

/// A synthetic voxel lattice: ground plane at y=0 plus whatever flora we add.
struct Lattice {
    std::set<std::tuple<int, int, int>> flora;
    int groundY = 0;   // solid, non-flora, at y <= groundY

    void addFlora(int x, int y, int z) { flora.insert({x, y, z}); }
    void trunk(int x, int z, int y0, int y1) {
        for (int y = y0; y <= y1; ++y) addFlora(x, y, z);
    }
    /// A 3x3x2 canopy blob centred on (cx, cz), sitting at cy..cy+1.
    void canopy(int cx, int cy, int cz) {
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = 0; dy <= 1; ++dy) addFlora(cx + dx, cy + dy, cz + dz);
    }

    std::function<bool(const glm::ivec3&)> floraProbe() const {
        return [this](const glm::ivec3& p) {
            return flora.count({p.x, p.y, p.z}) > 0;
        };
    }
    std::function<bool(const glm::ivec3&)> solidProbe() const {
        return [this](const glm::ivec3& p) {
            if (p.y <= groundY) return true;                 // the ground plane
            return flora.count({p.x, p.y, p.z}) > 0;
        };
    }
};

SweepBounds box(int lo, int hi) {
    return SweepBounds{glm::ivec3(lo, lo, lo), glm::ivec3(hi, hi, hi)};
}

bool contains(const std::vector<glm::ivec3>& v, const glm::ivec3& p) {
    for (const auto& c : v)
        if (c == p) return true;
    return false;
}

}  // namespace

// A tree standing on the ground is healthy — nothing is swept.
TEST(FloraSweepTest, AnIntactTreeIsNeverSwept) {
    Lattice L;
    L.trunk(10, 10, 1, 6);
    L.canopy(10, 7, 10);
    const auto orphans = planOrphanedFloraSweep(box(5, 15), L.floraProbe(), L.solidProbe());
    EXPECT_TRUE(orphans.empty()) << orphans.size() << " cells of a healthy tree were swept";
}

// The reported bug: the trunk was cleared by site prep, the canopy hung in the air.
TEST(FloraSweepTest, ACanopyLeftByAClearedTrunkIsSwept) {
    Lattice L;
    L.trunk(10, 10, 1, 6);
    L.canopy(10, 7, 10);
    // Site prep wipes the trunk column (a plot box / road corridor cleared it).
    for (int y = 1; y <= 6; ++y) L.flora.erase({10, y, 10});

    const auto orphans = planOrphanedFloraSweep(box(5, 15), L.floraProbe(), L.solidProbe());
    ASSERT_FALSE(orphans.empty()) << "the floating canopy was left in the air";
    // Every canopy cell, and nothing else.
    EXPECT_EQ(orphans.size(), 18u);
    for (const auto& c : orphans) EXPECT_GE(c.y, 7) << "swept a cell that was not canopy";
    EXPECT_TRUE(contains(orphans, glm::ivec3(10, 7, 10)));
    EXPECT_TRUE(contains(orphans, glm::ivec3(9, 8, 11)));
}

// The false positive that matters: a NEIGHBOURING healthy tree's canopy overhangs
// the cleared area. It is connected to a standing trunk, so it stays.
TEST(FloraSweepTest, AHealthyNeighboursOverhangSurvives) {
    Lattice L;
    // Cleared tree (trunk gone) at x=10, and a healthy tree at x=13 whose canopy
    // reaches across to x=11 — the two canopies TOUCH.
    L.canopy(10, 7, 10);
    L.trunk(13, 10, 1, 6);
    L.canopy(12, 7, 10);   // spans x 11..13 — overlaps the orphan blob's x=11 column

    const auto orphans = planOrphanedFloraSweep(box(5, 15), L.floraProbe(), L.solidProbe());
    EXPECT_TRUE(orphans.empty())
        << "swept " << orphans.size() << " cells of a canopy that is still connected to a "
           "standing trunk — a healthy tree was destroyed";
}

// Support may lie OUTSIDE the scan box: a component touching the boundary is left
// alone (never guess support away). Widening the box then reveals the truth.
TEST(FloraSweepTest, ComponentsLeavingTheBoxAreLeftAlone) {
    Lattice L;
    L.canopy(10, 7, 10);                  // orphan blob spans x 9..11, z 9..11, y 7..8
    // Narrow box whose face cuts through the blob.
    const SweepBounds tight{glm::ivec3(9, 7, 9), glm::ivec3(11, 8, 11)};
    const auto touching = planOrphanedFloraSweep(tight, L.floraProbe(), L.solidProbe());
    EXPECT_TRUE(touching.empty())
        << "swept a component that reaches the box boundary — its support is unknown";

    // The same blob inside a roomy box IS provably unsupported.
    const auto enclosed = planOrphanedFloraSweep(box(5, 15), L.floraProbe(), L.solidProbe());
    EXPECT_EQ(enclosed.size(), 18u);
}

// Flora resting on a BUILDING (not the ground) is supported too — a vine or a
// canopy lying on a roof is not orphaned.
TEST(FloraSweepTest, FloraRestingOnAStructureIsSupported) {
    Lattice L;
    L.canopy(10, 7, 10);
    // A roof slab directly under the blob: solid, not flora.
    auto solid = [&L](const glm::ivec3& p) {
        if (p.y <= L.groundY) return true;
        if (p.y == 6 && p.x >= 9 && p.x <= 11 && p.z >= 9 && p.z <= 11) return true;  // roof
        return L.flora.count({p.x, p.y, p.z}) > 0;
    };
    const auto orphans = planOrphanedFloraSweep(box(5, 15), L.floraProbe(), solid);
    EXPECT_TRUE(orphans.empty()) << "swept flora that rests on a structure";
}

TEST(FloraSweepTest, DeterministicAndEmptyOnACleanSite) {
    Lattice L;   // no flora at all
    EXPECT_TRUE(planOrphanedFloraSweep(box(0, 20), L.floraProbe(), L.solidProbe()).empty());

    Lattice T;
    T.canopy(10, 7, 10);
    const auto a = planOrphanedFloraSweep(box(5, 15), T.floraProbe(), T.solidProbe());
    const auto b = planOrphanedFloraSweep(box(5, 15), T.floraProbe(), T.solidProbe());
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) EXPECT_EQ(a[i], b[i]) << "sweep order drifted at " << i;
}
