// VoxFoundBodiesTest -- docs/VoxelFoundationPlan.md §6.5 step 3 + §6.7, F5.1 extraction.
// Predictions written before the code: docs/evidence/voxfound_f51_prediction.txt.
//
// Every check here is independent of the extraction code: conservation is counted voxel by voxel against the
// grid before the hit and the FractureResult; mass properties against a brute-force per-voxel sum (a different
// method from the implementation's moments) and, for a box, the closed form; momentum against the hit.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "core/voxfound/Bodies.h"
#include "core/voxfound/FractureLab.h"

using namespace Phyxel::Core::VoxFound;

namespace {

Rig rigAt(int k, float e) {
    switch (k % 6) {
        case 0: return rigStoneWall({12, 8, 0}, e);
        case 1: return rigBrickWall(e);
        case 2: return rigBeam(e);
        case 3: return rigGlassPane(e);
        case 4: return rigColumnLintel(e);
        default: return rigChair(e);
    }
}

double relErr(double a, double b, double scale) { return std::abs(a - b) / std::max(scale, 1e-300); }

double matScale(const glm::dmat3& m) {
    double s = 0.0;
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) s = std::max(s, std::abs(m[c][r]));
    return s;
}

struct CheckStats {
    size_t bodies = 0, crumbs = 0, bodyVoxels = 0;
};

// P1 + P2 + P3 (brute force) + P4 on one extraction. `before` is the grid the fracture ran on.
void checkExtraction(const VoxelGrid& before, const VoxelGrid& after, const FractureResult& r, const Extraction& ex,
                     const Hit& hit, const std::vector<FractureMaterial>& mats, CheckStats* st = nullptr) {
    ASSERT_TRUE(ex.implemented) << "extractBodies is not implemented";
    ASSERT_EQ(after.dim, before.dim);
    const size_t n = before.size();
    std::vector<uint32_t> seen(n, 0);
    size_t expectBodies = 0;
    for (size_t id = 1; id < r.pieceKind.size(); ++id) expectBodies += r.pieceKind[id] == PieceKind::Body;
    ASSERT_EQ(ex.bodies.size(), expectBodies);

    std::vector<size_t> perMat(mats.size(), 0);
    uint32_t prevId = 0;
    glm::dvec3 momentum(0.0);
    const double vm = before.voxelM;
    for (const Body& b : ex.bodies) {
        ASSERT_GT(b.pieceId, prevId) << "bodies not in ascending piece id";
        prevId = b.pieceId;
        ASSERT_LT(b.pieceId, r.pieceKind.size());
        ASSERT_EQ(r.pieceKind[b.pieceId], PieceKind::Body);
        ASSERT_EQ(b.grid.voxelM, before.voxelM);
        ASSERT_EQ(b.grid.damage.size(), b.grid.size());
        uint32_t count = 0;
        bool contactInBody = false;
        double nearest = 1e300;
        for (size_t j = 0; j < b.grid.size(); ++j) {
            if (!b.grid.mat[j]) continue;
            const glm::ivec3 w = b.origin + b.grid.pos(j);
            ASSERT_TRUE(before.in(w));
            const size_t i = before.idx(w);
            ASSERT_EQ(r.piece[i], b.pieceId) << "a body voxel from another piece";
            ASSERT_EQ(b.grid.mat[j], before.mat[i]);
            ASSERT_EQ(b.grid.part[j], before.part[i]);
            ASSERT_EQ(b.grid.fixed[j], 0);
            ASSERT_EQ(b.grid.damage[j], r.damage[i]);
            ++seen[i];
            ++perMat[b.grid.mat[j]];
            ++count;
            contactInBody |= w == b.contact;
            nearest = std::min(nearest, glm::length(glm::dvec3(w) + 0.5 - glm::dvec3(hit.point)));
        }
        ASSERT_EQ(count, r.pieceSize[b.pieceId]);
        ASSERT_EQ(b.voxels, count);
        ASSERT_TRUE(contactInBody) << "the contact voxel is not in the body";
        ASSERT_NEAR(glm::length(glm::dvec3(b.contact) + 0.5 - glm::dvec3(hit.point)), nearest, 1e-9)
            << "the contact is not the voxel nearest the hit";

        double m;
        glm::dvec3 c;
        glm::dmat3 I;
        bodyMassBruteForce(b, mats, m, c, I);
        ASSERT_GT(m, 0.0);
        ASSERT_LE(relErr(b.mass, m, m), 1e-9);
        for (int a = 0; a < 3; ++a) ASSERT_LE(std::abs(b.com[a] - c[a]), 1e-9 * (1.0 + std::abs(c[a])));
        const double s = matScale(I);
        for (int cc = 0; cc < 3; ++cc)
            for (int rr = 0; rr < 3; ++rr) ASSERT_LE(relErr(b.inertia[cc][rr], I[cc][rr], s), 1e-9);

        // P4: I omega = (x - c) x dp, dp = m v
        const glm::dvec3 dp = b.mass * b.vel;
        momentum += dp;
        const glm::dvec3 arm = (glm::dvec3(b.contact) + 0.5 - b.com) * vm;
        const glm::dvec3 L = glm::cross(arm, dp);
        const glm::dvec3 Iw = b.inertia * b.angVel;
        const double ls = glm::length(L) + glm::length(dp) * vm;
        for (int a = 0; a < 3; ++a) ASSERT_LE(std::abs(Iw[a] - L[a]), 1e-9 * std::max(ls, 1e-30));
        if (st) st->bodyVoxels += count;
    }
    if (!ex.bodies.empty()) {
        const glm::dvec3 P = double(hit.impulse) * glm::dvec3(hit.dir);
        const double ps = std::max(glm::length(P), 1e-30);
        for (int a = 0; a < 3; ++a) ASSERT_LE(std::abs(momentum[a] - P[a]), 1e-9 * ps) << "momentum not conserved";
    }

    size_t prevCrumb = 0;
    bool firstCrumb = true;
    for (const Crumb& c : ex.crumbs) {
        ASSERT_TRUE(before.in(c.pos));
        const size_t i = before.idx(c.pos);
        ASSERT_TRUE(firstCrumb || i > prevCrumb) << "crumbs not in ascending index";
        firstCrumb = false;
        prevCrumb = i;
        ASSERT_NE(r.piece[i], 0u);
        ASSERT_EQ(r.pieceKind[r.piece[i]], PieceKind::Crumb);
        ASSERT_EQ(c.mat, before.mat[i]);
        ASSERT_EQ(c.part, before.part[i]);
        ASSERT_EQ(c.damage, r.damage[i]);
        ++seen[i];
        ++perMat[c.mat];
    }

    // every voxel: anchored untouched (damage applied), loose removed and seen exactly once
    for (size_t i = 0; i < n; ++i) {
        if (!before.mat[i]) {
            ASSERT_EQ(after.mat[i], 0) << "the world gained a voxel at " << i;
            ASSERT_EQ(seen[i], 0u);
            continue;
        }
        const PieceKind k = r.pieceKind[r.piece[i]];
        if (k == PieceKind::Anchored) {
            ASSERT_EQ(after.mat[i], before.mat[i]);
            ASSERT_EQ(after.part[i], before.part[i]);
            ASSERT_EQ(after.fixed[i], before.fixed[i]);
            ASSERT_EQ(after.damage.size(), n);
            ASSERT_EQ(after.damage[i], r.damage[i]);
            ASSERT_EQ(seen[i], 0u);
            ++perMat[after.mat[i]];
        } else {
            ASSERT_EQ(after.mat[i], 0) << "a loose voxel stayed in the world at " << i;
            ASSERT_EQ(seen[i], 1u) << "a loose voxel is not in exactly one body / crumb at " << i;
        }
    }
    for (size_t m = 1; m < mats.size(); ++m) ASSERT_EQ(perMat[m], before.count(uint8_t(m))) << "material " << m;
    if (st) {
        st->bodies += ex.bodies.size();
        st->crumbs += ex.crumbs.size();
    }
}

void bodiesEqual(const Extraction& a, const Extraction& b) {
    ASSERT_EQ(a.bodies.size(), b.bodies.size());
    ASSERT_EQ(a.crumbs.size(), b.crumbs.size());
    for (size_t k = 0; k < a.bodies.size(); ++k) {
        const Body& x = a.bodies[k];
        const Body& y = b.bodies[k];
        ASSERT_EQ(x.pieceId, y.pieceId);
        ASSERT_EQ(x.origin, y.origin);
        ASSERT_EQ(x.grid.mat, y.grid.mat);
        ASSERT_EQ(x.grid.damage, y.grid.damage);
        ASSERT_EQ(x.mass, y.mass);
        ASSERT_EQ(x.com, y.com);
        ASSERT_EQ(x.vel, y.vel);
        ASSERT_EQ(x.angVel, y.angVel);
    }
    for (size_t k = 0; k < a.crumbs.size(); ++k) ASSERT_EQ(a.crumbs[k].pos, b.crumbs[k].pos);
}

}  // namespace

// P3: a free 8 x 4 x 2 stone box is one loose body with closed-form mass properties
TEST(VoxFoundBodies, FreeBoxMassPropertiesAreAnalytic) {
    VoxelGrid g;
    g.resize({12, 8, 6});
    g.voxelM = 0.125f;
    for (int x = 2; x < 10; ++x)
        for (int y = 3; y < 7; ++y)
            for (int z = 1; z < 3; ++z) g.set({x, y, z}, kStone);
    const auto mats = labMaterials();
    const Hit hit{{2.0f, 5.0f, 2.0f}, {1, 0, 0}, 0.0f, 9, 30.0f};
    const FractureResult r = fractureReference(g, hit, mats);
    const VoxelGrid before = g;
    const Extraction ex = extractBodies(g, r, hit, mats);
    checkExtraction(before, g, r, ex, hit, mats);
    if (HasFatalFailure()) return;
    ASSERT_EQ(ex.bodies.size(), 1u);
    const Body& b = ex.bodies[0];
    const double v = 0.125, m = 64 * 2600.0 * v * v * v;
    const double a = 8 * v, bb = 4 * v, c = 2 * v;
    EXPECT_LE(relErr(b.mass, m, m), 1e-9);
    EXPECT_LE(glm::length(b.com - glm::dvec3(6.0, 5.0, 2.0)), 1e-9);
    const double Ixx = m * (bb * bb + c * c) / 12, Iyy = m * (a * a + c * c) / 12, Izz = m * (a * a + bb * bb) / 12;
    EXPECT_LE(relErr(b.inertia[0][0], Ixx, Izz), 1e-9);
    EXPECT_LE(relErr(b.inertia[1][1], Iyy, Izz), 1e-9);
    EXPECT_LE(relErr(b.inertia[2][2], Izz, Izz), 1e-9);
    EXPECT_LE(std::abs(b.inertia[0][1]) + std::abs(b.inertia[0][2]) + std::abs(b.inertia[1][2]), 1e-9 * Izz);
    // a push along x on the -x face: v = P / m along x (it spins too: the nearest voxel is off the centre line)
    EXPECT_LE(glm::length(b.vel - glm::dvec3(30.0 / m, 0, 0)), 1e-9 * 30.0 / m);
    std::printf("[bodies] free box: m %.3f kg, I %.5f / %.5f / %.5f kg m^2, v %.5f m/s, |w| %.2e\n", b.mass,
                b.inertia[0][0], b.inertia[1][1], b.inertia[2][2], b.vel.x, glm::length(b.angVel));
}

// P1 + P2 + P3 + P4 on every §8 rig over the F4.1 energy sweep, twice (determinism)
TEST(VoxFoundBodies, EveryRigConservesAndBodiesMatchBruteForce) {
    const float sweep[] = {1.5f, 2, 3, 5, 8, 12, 20, 30, 50, 80};
    CheckStats st;
    for (int k = 0; k < 6; ++k)
        for (float e : sweep) {
            Rig rig = rigAt(k, e);
            rig.hit.impulse = 40.0f;
            SCOPED_TRACE(rig.name + " E/T " + std::to_string(e));
            const FractureResult r = fractureReference(rig.grid, rig.hit, rig.mats);
            VoxelGrid w1 = rig.grid, w2 = rig.grid;
            const Extraction a = extractBodies(w1, r, rig.hit, rig.mats);
            checkExtraction(rig.grid, w1, r, a, rig.hit, rig.mats, &st);
            if (HasFatalFailure()) return;
            const Extraction b = extractBodies(w2, r, rig.hit, rig.mats);
            bodiesEqual(a, b);
            if (HasFatalFailure()) return;
            ASSERT_EQ(w1.mat, w2.mat);
        }
    std::printf("[bodies] rigs x sweep: %zu bodies (%zu voxels), %zu crumbs\n", st.bodies, st.bodyVoxels, st.crumbs);
    EXPECT_GT(st.bodies, 0u);
    EXPECT_GT(st.crumbs, 0u);
}

// P1 + P2 + P4 under 300 random hits (the F4.1 stress distribution)
TEST(VoxFoundBodies, RandomHitsConserve) {
    std::mt19937_64 rng(5101);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    CheckStats st;
    for (int h = 0; h < 300; ++h) {
        Rig rig = rigAt(h % 6, 1.0f);
        size_t i = 0;
        do { i = size_t(U(rng) * double(rig.grid.size())) % rig.grid.size(); } while (!rig.grid.mat[i]);
        const int axis = int(U(rng) * 3) % 3;
        glm::vec3 dir(0.0f);
        dir[axis] = U(rng) < 0.5 ? -1.0f : 1.0f;
        const double eot = 0.3 * std::pow(100.0 / 0.3, U(rng));
        const uint8_t m = rig.grid.mat[i];
        rig.hit = {glm::vec3(rig.grid.pos(i)) + 0.5f, dir, float(eot) * rig.mats[m].toughness, rng(),
                   float(1.0 + 99.0 * U(rng))};
        SCOPED_TRACE(rig.name + " hit " + std::to_string(h));
        const FractureResult r = fractureReference(rig.grid, rig.hit, rig.mats);
        VoxelGrid w = rig.grid;
        const Extraction ex = extractBodies(w, r, rig.hit, rig.mats);
        checkExtraction(rig.grid, w, r, ex, rig.hit, rig.mats, &st);
        if (HasFatalFailure()) return;
    }
    std::printf("[bodies] 300 random hits: %zu bodies (%zu voxels), %zu crumbs\n", st.bodies, st.bodyVoxels,
                st.crumbs);
    EXPECT_GT(st.bodies, 0u);
}

// P5: a wall hit 20 times in a row, each on the world the previous extraction left; exact after every hit
TEST(VoxFoundBodies, RepeatedHitsOnAWallConserveEveryStep) {
    Rig rig = rigStoneWall({12, 8, 0}, 1.0f);
    std::mt19937_64 rng(5102);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    const size_t start = rig.grid.solidCount();
    size_t removed = 0, bodies = 0, crumbs = 0;
    for (int h = 0; h < 20; ++h) {
        size_t i = 0;
        do { i = size_t(U(rng) * double(rig.grid.size())) % rig.grid.size(); } while (!rig.grid.mat[i]);
        const double eot = 2.0 + 30.0 * U(rng);
        rig.hit = {glm::vec3(rig.grid.pos(i)) + 0.5f, {0, 0, 1}, float(eot) * rig.mats[kStone].toughness, rng(),
                   25.0f};
        SCOPED_TRACE("hit " + std::to_string(h));
        const FractureResult r = fractureReference(rig.grid, rig.hit, rig.mats);
        const VoxelGrid before = rig.grid;
        const Extraction ex = extractBodies(rig.grid, r, rig.hit, rig.mats);
        checkExtraction(before, rig.grid, r, ex, rig.hit, rig.mats);
        if (HasFatalFailure()) return;
        size_t inBodies = 0;
        for (const Body& b : ex.bodies) inBodies += b.voxels;
        removed += inBodies + ex.crumbs.size();
        bodies += ex.bodies.size();
        crumbs += ex.crumbs.size();
        ASSERT_EQ(rig.grid.solidCount() + removed, start);
    }
    std::printf("[bodies] 20 hits in a row: %zu of %zu voxels removed, %zu bodies, %zu crumbs\n", removed, start,
                bodies, crumbs);
    EXPECT_GT(removed, 0u);
}

// P6: cost on the F4.3 large wall (reported, not gated)
TEST(VoxFoundBodies, CostOnALargeWall) {
    VoxelGrid g;
    g.resize({256, 128, 4});
    g.voxelM = 0.125f;
    for (int x = 0; x < 256; ++x)
        for (int y = 0; y < 128; ++y)
            for (int z = 0; z < 4; ++z) g.set({x, y, z}, kStone, 0, y == 0);
    const auto mats = labMaterials();
    for (float e : {150.0f, 1000.0f, 2500.0f}) {
        const Hit hit{{128, 64, 0}, {0, 0, 1}, e * mats[kStone].toughness, 777, 200.0f};
        const FractureResult r = fractureReference(g, hit, mats);
        double best = 1e30;
        Extraction ex;
        VoxelGrid w;
        for (int rep = 0; rep < 5; ++rep) {
            w = g;
            const auto t0 = std::chrono::steady_clock::now();
            ex = extractBodies(w, r, hit, mats);
            best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        checkExtraction(g, w, r, ex, hit, mats);
        if (HasFatalFailure()) return;
        std::printf("[bodies] large wall E/T %5.0f: %zu bodies, %zu crumbs, extraction %.2f ms (best of 5)\n", e,
                    ex.bodies.size(), ex.crumbs.size(), best);
    }
}
