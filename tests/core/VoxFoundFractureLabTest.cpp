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
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>

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

::testing::AssertionResult conserved(const VoxelGrid& g, const FractureResult& r, size_t nMats);

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
    EXPECT_TRUE(conserved(rig.grid, a, rig.mats.size()));
    const FractureResult b = fractureReference(rig.grid, rig.hit, rig.mats);
    EXPECT_EQ(a.cut, b.cut);
    EXPECT_EQ(a.piece, b.piece);
    EXPECT_EQ(a.damage, b.damage);
    const CutStats s = cutStats(rig.grid, a.cut);
    if (expectCuts) EXPECT_GT(s.cut, 0u);
    else EXPECT_EQ(s.cut, 0u);
}

// Conservation (§6.5 point 4): per material, anchored + bodies + crumbs = before, exactly; and the piece
// table agrees with the per-voxel labels.
::testing::AssertionResult conserved(const VoxelGrid& g, const FractureResult& r, size_t nMats) {
    std::vector<size_t> before(nMats, 0), after(nMats * 3, 0);
    std::vector<uint32_t> size(r.pieceSize.size(), 0);
    for (size_t i = 0; i < g.size(); ++i) {
        if (!g.mat[i]) continue;
        ++before[g.mat[i]];
        const uint32_t id = r.piece[i];
        if (id == 0 || id >= r.pieceSize.size()) return ::testing::AssertionFailure() << "voxel without a piece";
        ++size[id];
        ++after[size_t(g.mat[i]) * 3 + size_t(r.pieceKind[id])];
    }
    for (size_t id = 1; id < size.size(); ++id) {
        if (size[id] != r.pieceSize[id]) return ::testing::AssertionFailure() << "pieceSize wrong for " << id;
        if ((r.pieceKind[id] == PieceKind::Anchored) != (r.pieceAnchored[id] != 0))
            return ::testing::AssertionFailure() << "kind / anchored disagree for " << id;
    }
    for (size_t m = 1; m < nMats; ++m)
        if (before[m] != after[m * 3] + after[m * 3 + 1] + after[m * 3 + 2])
            return ::testing::AssertionFailure() << "material " << m << " not conserved";
    return ::testing::AssertionSuccess();
}

size_t looseVoxels(const FractureResult& r) {
    size_t n = 0;
    for (size_t id = 1; id < r.pieceSize.size(); ++id)
        if (!r.pieceAnchored[id]) n += r.pieceSize[id];
    return n;
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
    std::printf("[fracture lab] control: damage at the hit voxel %u\n", unsigned(r.damage[rig.grid.idx({12, 8, 0})]));
}

// ---- F4.1 stress: the scaling dimension is hits (§6.9 determinism, §6.5 conservation) ------------------

namespace {
Rig rigByIndex(int k) {
    switch (k) {
        case 0: return rigStoneWall({12, 8, 0}, 1.0f);
        case 1: return rigBrickWall(1.0f);
        case 2: return rigBeam(1.0f);
        case 3: return rigGlassPane(1.0f);
        case 4: return rigColumnLintel(1.0f);
        default: return rigChair(1.0f);
    }
}
}  // namespace

TEST(VoxFoundFractureLab, StressRandomHitsAgreeConserveNestAndAccumulate) {
    std::mt19937_64 rng(4101);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    size_t nested = 0, progressive = 0, cutTotal = 0;
    for (int h = 0; h < 300; ++h) {
        Rig rig = rigByIndex(h % 6);
        const glm::ivec3 dim = rig.grid.dim;
        // a random solid voxel; the hit lands on its centre, from a random axis direction
        size_t i = 0;
        do { i = size_t(U(rng) * double(rig.grid.size())) % rig.grid.size(); } while (!rig.grid.mat[i]);
        const int axis = int(U(rng) * 3) % 3;
        glm::vec3 dir(0.0f);
        dir[axis] = U(rng) < 0.5 ? -1.0f : 1.0f;
        const double eot = 0.3 * std::pow(100.0 / 0.3, U(rng));
        const uint8_t m = rig.grid.mat[i];
        rig.hit = {glm::vec3(rig.grid.pos(i)) + 0.5f, dir, float(eot) * rig.mats[m].toughness, rng()};
        SCOPED_TRACE(rig.name + " hit " + std::to_string(h) + " E/T " + std::to_string(eot) + " dim " +
                     std::to_string(dim.x));

        const FractureResult a = fractureReference(rig.grid, rig.hit, rig.mats);
        std::string why;
        ASSERT_TRUE(resultMatchesChecker(rig.grid, a, &why)) << why;
        ASSERT_TRUE(conserved(rig.grid, a, rig.mats.size()));
        const FractureResult b = fractureReference(rig.grid, rig.hit, rig.mats);
        ASSERT_EQ(a.cut, b.cut);
        ASSERT_EQ(a.piece, b.piece);
        ASSERT_EQ(a.damage, b.damage);
        cutTotal += cutStats(rig.grid, a.cut).cut;

        // tier knob: the crumb threshold changes kinds only, never pieces
        FractureKnobs none;
        none.crumbVoxels = 0;
        const FractureResult k0 = fractureReference(rig.grid, rig.hit, rig.mats, none);
        ASSERT_EQ(a.piece, k0.piece);
        ASSERT_EQ(a.cut, k0.cut);

        // nesting: twice the energy cuts a superset (brittle / fibrous; glass is not nested)
        if (rig.mats[m].cls != FractureClass::Glass) {
            Hit h2 = rig.hit;
            h2.energy *= 2.0f;
            const FractureResult c = fractureReference(rig.grid, h2, rig.mats);
            for (size_t v = 0; v < rig.grid.size(); ++v)
                ASSERT_EQ(a.cut[v] & ~c.cut[v], 0) << "a cut vanished at 2x energy, voxel " << v;
            ASSERT_GE(looseVoxels(c), looseVoxels(a));
            ++nested;
        }

        // progressive damage: the same blow again, on the damaged grid, cuts at least as much
        VoxelGrid g2 = rig.grid;
        applyDamage(g2, a);
        const FractureResult d = fractureReference(g2, rig.hit, rig.mats);
        for (size_t v = 0; v < rig.grid.size(); ++v)
            ASSERT_EQ(a.cut[v] & ~d.cut[v], 0) << "a cut vanished on the damaged grid, voxel " << v;
        ASSERT_TRUE(conserved(g2, d, rig.mats.size()));
        if (cutStats(g2, d.cut).cut > cutStats(rig.grid, a.cut).cut) ++progressive;
    }
    std::printf("[fracture lab] stress: 300 hits, %zu cuts, %zu nesting checks, %zu hits where a repeat cut more\n",
                cutTotal, nested, progressive);
    EXPECT_GT(progressive, 0u);
}

// ---- F4.1 measured summary (+ the picture dump when VOXFOUND_FRACTURE_DUMP names a directory) -----------

TEST(VoxFoundFractureLab, SummaryAndPictureDump) {
    struct Case { std::string tag; Rig rig; };
    std::vector<Case> cases;
    for (float e : {2.0f, 5.0f, 12.0f, 30.0f, 80.0f})
        cases.push_back({"stone_centre_e" + std::to_string(int(e)), rigStoneWall({12, 8, 0}, e)});
    cases.push_back({"stone_edge_e5", rigStoneWall({22, 14, 0}, 5.0f)});
    cases.push_back({"brick_e8", rigBrickWall(8.0f)});
    cases.push_back({"brick_e30", rigBrickWall(30.0f)});
    cases.push_back({"beam_e8", rigBeam(8.0f)});
    cases.push_back({"glass_e20", rigGlassPane(20.0f)});
    cases.push_back({"column_e30", rigColumnLintel(30.0f)});
    cases.push_back({"chair_e8", rigChair(8.0f)});
    const char* dir = std::getenv("VOXFOUND_FRACTURE_DUMP");
    for (const Case& c : cases) {
        const Rig& rig = c.rig;
        double best = 1e30;
        FractureResult r;
        for (int rep = 0; rep < 5; ++rep) {
            const auto t0 = std::chrono::steady_clock::now();
            r = fractureReference(rig.grid, rig.hit, rig.mats);
            best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        const CutStats s = cutStats(rig.grid, r.cut);
        size_t bodies = 0, crumbs = 0, bodyVox = 0, crumbVox = 0, anchoredVox = 0;
        for (size_t id = 1; id < r.pieceSize.size(); ++id) {
            if (r.pieceKind[id] == PieceKind::Body) { ++bodies; bodyVox += r.pieceSize[id]; }
            else if (r.pieceKind[id] == PieceKind::Crumb) { ++crumbs; crumbVox += r.pieceSize[id]; }
            else anchoredVox += r.pieceSize[id];
        }
        std::printf("[fracture lab] %-16s voxels %6zu cuts %5zu (between parts %5zu; x %4zu y %4zu z %4zu) "
                    "bodies %3zu (%5zu vox) crumbs %3zu (%4zu vox) anchored %5zu vox  %.2f ms\n",
                    c.tag.c_str(), rig.grid.solidCount(), s.cut, s.betweenParts, s.alongX, s.alongY, s.alongZ, bodies,
                    bodyVox, crumbs, crumbVox, anchoredVox, best);
        if (!dir) continue;
        std::ofstream f(std::string(dir) + "/" + c.tag + ".txt");
        f << "dim " << rig.grid.dim.x << " " << rig.grid.dim.y << " " << rig.grid.dim.z << "\n";
        f << "hit " << rig.hit.point.x << " " << rig.hit.point.y << " " << rig.hit.point.z << " " << rig.hit.dir.x << " "
          << rig.hit.dir.y << " " << rig.hit.dir.z << "\n";
        for (size_t i = 0; i < rig.grid.size(); ++i) {
            if (!rig.grid.mat[i]) continue;
            const glm::ivec3 p = rig.grid.pos(i);
            f << p.x << " " << p.y << " " << p.z << " " << int(rig.grid.mat[i]) << " " << r.piece[i] << " "
              << int(r.pieceKind[r.piece[i]]) << " " << int(r.cut[i]) << "\n";
        }
    }
}
