// VoxFoundFractureLabTest — docs/VoxelFoundationPlan.md §8 fracture lab (F0 skeleton).
//
// Every rig's prediction is written in the plan (§8) and in the rig (FractureLab.cpp) BEFORE the model
// exists. fractureReference() is not implemented until F4, so the prediction tests are RED now; the
// checker self-tests (the independent piece labeller and cut statistics) are green, because F4's
// result is judged by them.
//
// Common checks per rig (§8): the result agrees with the independent checker (pieces + anchoring),
// two runs are identical (determinism), materials are untouched (a break moves nothing by itself:
// conservation), and something was cut (except the control).

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/voxfound/FractureLab.h"

using namespace Phyxel::Core::VoxFound;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Outcome {
    FractureResult r;
    PieceLabels L;
    CutStats s;
    size_t looseVoxels = 0;
    uint32_t loosePieces = 0;
};

Outcome run(const Rig& rig) {
    Outcome o;
    o.r = fractureReference(rig.grid, rig.hit, rig.mats);
    o.L = labelPieces(rig.grid, o.r.cut);
    o.s = cutStats(rig.grid, o.r.cut);
    for (uint32_t id = 1; id <= o.L.pieces(); ++id)
        if (!o.L.anchored[id]) { o.looseVoxels += o.L.size[id]; ++o.loosePieces; }
    return o;
}

void commonChecks(const Rig& rig, bool expectCuts) {
    SCOPED_TRACE(rig.name);
    const FractureResult a = fractureReference(rig.grid, rig.hit, rig.mats);
    ASSERT_TRUE(a.implemented) << "fractureReference is not implemented yet (F4)";
    std::string why;
    EXPECT_TRUE(resultMatchesChecker(rig.grid, a, &why)) << why;
    const FractureResult b = fractureReference(rig.grid, rig.hit, rig.mats);
    EXPECT_EQ(a.cut, b.cut);
    EXPECT_EQ(a.piece, b.piece);
    EXPECT_EQ(a.damage, b.damage);
    const CutStats s = cutStats(rig.grid, a.cut);
    if (expectCuts) EXPECT_GT(s.cut, 0u);
    else EXPECT_EQ(s.cut, 0u);
}

}  // namespace

// ---- the checker itself (green: F4 is judged by it) ------------------------------------------

TEST(VoxFoundFractureLab, CheckerLabelsUncutGridAsOnePieceAnchoredByItsBase) {
    const Rig rig = rigStoneWall({12, 8, 0}, 3.0f);
    const PieceLabels L = labelPieces(rig.grid, {});
    ASSERT_EQ(L.pieces(), 1u);
    EXPECT_EQ(L.size[1], rig.grid.solidCount());
    EXPECT_TRUE(L.anchored[1]);
}

TEST(VoxFoundFractureLab, CheckerSplitsAlongAFullCutPlaneAndFindsTheLoosePart) {
    const Rig rig = rigStoneWall({12, 8, 0}, 3.0f);
    std::vector<uint8_t> cut(rig.grid.size(), 0);
    for (int x = 0; x < rig.grid.dim.x; ++x)
        for (int z = 0; z < rig.grid.dim.z; ++z) cut[rig.grid.idx({x, 9, z})] |= CutPY;  // between y 9 and 10
    const PieceLabels L = labelPieces(rig.grid, cut);
    ASSERT_EQ(L.pieces(), 2u);
    const uint32_t top = L.label[rig.grid.idx({0, 15, 0})];
    const uint32_t bottom = L.label[rig.grid.idx({0, 0, 0})];
    EXPECT_NE(top, bottom);
    EXPECT_FALSE(L.anchored[top]);
    EXPECT_TRUE(L.anchored[bottom]);
    EXPECT_EQ(L.size[top], size_t(24 * 6 * 3));
    const CutStats s = cutStats(rig.grid, cut);
    EXPECT_EQ(s.cut, size_t(24 * 3));
    EXPECT_EQ(s.alongY, s.cut);
}

TEST(VoxFoundFractureLab, CheckerCountsMortarCutsInTheBrickWall) {
    const Rig rig = rigBrickWall(3.0f);
    // cut every bond between different bricks: all cuts are between parts, and the wall falls into bricks
    std::vector<uint8_t> cut(rig.grid.size(), 0);
    for (size_t i = 0; i < rig.grid.size(); ++i) {
        const glm::ivec3 p = rig.grid.pos(i);
        const glm::ivec3 d[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        for (int a = 0; a < 3; ++a) {
            const glm::ivec3 q = p + d[a];
            if (rig.grid.in(q) && rig.grid.part[rig.grid.idx(q)] != rig.grid.part[i]) cut[i] |= uint8_t(1u << a);
        }
    }
    const CutStats s = cutStats(rig.grid, cut);
    EXPECT_GT(s.cut, 0u);
    EXPECT_EQ(s.betweenParts, s.cut);
    const PieceLabels L = labelPieces(rig.grid, cut);
    // 8 courses; even courses 8 whole bricks, odd courses 7 whole + 2 half bricks at the ends
    EXPECT_EQ(L.pieces(), 4u * 8u + 4u * 9u);
}

TEST(VoxFoundFractureLab, CheckerRejectsAnUnimplementedResult) {
    const Rig rig = rigStoneWall({12, 8, 0}, 3.0f);
    std::string why;
    EXPECT_FALSE(resultMatchesChecker(rig.grid, FractureResult{}, &why));
    EXPECT_NE(why.find("not implemented"), std::string::npos);
}

// ---- the predictions (RED until F4) -----------------------------------------------------------

TEST(VoxFoundFractureLab, StoneWallEnergySweep) {
    const float sweep[] = {1.5f, 2, 3, 5, 8, 12, 20, 30, 50, 80};
    size_t prevLoose = 0;
    Outcome lo, hi;
    for (float e : sweep) {
        const Rig rig = rigStoneWall({12, 8, 0}, e);
        commonChecks(rig, true);
        if (HasFatalFailure()) return;
        const Outcome o = run(rig);
        std::printf("[fracture lab] stone wall centre E/T %5.1f: %zu cuts, %u loose pieces, %zu loose voxels\n", e,
                    o.s.cut, o.loosePieces, o.looseVoxels);
        EXPECT_GE(o.looseVoxels, prevLoose) << "loose voxels decreased at E/T " << e;
        prevLoose = o.looseVoxels;
        if (e == sweep[1]) lo = o;
        if (e == sweep[9]) hi = o;
    }
    EXPECT_GT(hi.loosePieces, lo.loosePieces);
    if (lo.loosePieces && hi.loosePieces)
        EXPECT_LT(double(hi.looseVoxels) / hi.loosePieces, double(lo.looseVoxels) / lo.loosePieces + 1e-9);
}

TEST(VoxFoundFractureLab, StoneWallEdgeHitChipsAtLeastAsMuchAsCentre) {
    const Rig centre = rigStoneWall({12, 8, 0}, 5.0f);
    const Rig edge = rigStoneWall({22, 14, 0}, 5.0f);
    commonChecks(edge, true);
    if (HasFatalFailure()) return;
    EXPECT_GE(run(edge).looseVoxels, run(centre).looseVoxels);
}

TEST(VoxFoundFractureLab, BrickWallBreaksAlongMortar) {
    const Rig rig = rigBrickWall(8.0f);
    commonChecks(rig, true);
    if (HasFatalFailure()) return;
    const Outcome o = run(rig);
    std::printf("[fracture lab] brick wall: %zu cuts, %zu between bricks\n", o.s.cut, o.s.betweenParts);
    EXPECT_GT(double(o.s.betweenParts), 0.8 * double(o.s.cut));
}

TEST(VoxFoundFractureLab, BeamSplitsAlongTheGrain) {
    const Rig rig = rigBeam(8.0f);
    commonChecks(rig, true);
    if (HasFatalFailure()) return;
    const Outcome o = run(rig);
    std::printf("[fracture lab] beam: cuts x %zu, y %zu, z %zu\n", o.s.alongX, o.s.alongY, o.s.alongZ);
    EXPECT_GT(double(o.s.alongY + o.s.alongZ), 3.0 * double(o.s.alongX));
}

TEST(VoxFoundFractureLab, GlassPaneShattersRadially) {
    const Rig rig = rigGlassPane(20.0f);
    commonChecks(rig, true);
    if (HasFatalFailure()) return;
    const Outcome o = run(rig);
    bool sector[8] = {};
    for (uint32_t id = 1; id <= o.L.pieces(); ++id) {
        if (o.L.anchored[id]) continue;
        const glm::vec2 d = glm::vec2(o.L.centroid[id]) - glm::vec2(rig.hit.point) - glm::vec2(0.5f);
        if (glm::length(d) < 0.5f) continue;
        const double ang = std::atan2(d.y, d.x) + kPi;  // 0..2pi
        sector[std::min(7, int(ang / (kPi / 4.0)))] = true;
    }
    int covered = 0;
    for (bool s : sector) covered += s;
    std::printf("[fracture lab] glass: %u loose shards, %d of 8 sectors\n", o.loosePieces, covered);
    EXPECT_GE(o.loosePieces, 6u);
    EXPECT_GE(covered, 6);
}

TEST(VoxFoundFractureLab, SeveredColumnFreesTheLintel) {
    const Rig rig = rigColumnLintel(30.0f);
    commonChecks(rig, true);
    if (HasFatalFailure()) return;
    const Outcome o = run(rig);
    for (size_t i = 0; i < rig.grid.size(); ++i) {
        if (rig.grid.mat[i] == 0) continue;
        const glm::ivec3 p = rig.grid.pos(i);
        if (rig.grid.part[i] == 2) EXPECT_FALSE(o.L.anchored[o.L.label[i]]) << "lintel voxel anchored at y " << p.y;
        if (p.y == 0) EXPECT_TRUE(o.L.anchored[o.L.label[i]]);
    }
}

TEST(VoxFoundFractureLab, ChairBreaksAtItsJoints) {
    const Rig rig = rigChair(8.0f);
    commonChecks(rig, true);
    if (HasFatalFailure()) return;
    const Outcome o = run(rig);
    EXPECT_GT(double(o.s.betweenParts), 0.8 * double(o.s.cut));
    EXPECT_GE(o.L.pieces(), 2u);
}

TEST(VoxFoundFractureLab, ControlBelowToughnessCutsNothingAndAddsDamage) {
    const Rig rig = rigStoneWall({12, 8, 0}, 0.5f);
    commonChecks(rig, false);
    if (HasFatalFailure()) return;
    const FractureResult r = fractureReference(rig.grid, rig.hit, rig.mats);
    ASSERT_EQ(r.damage.size(), rig.grid.size());
    EXPECT_GT(r.damage[rig.grid.idx({12, 8, 0})], 0u);
}
