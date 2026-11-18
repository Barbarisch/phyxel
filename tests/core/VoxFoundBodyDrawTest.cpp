// VoxFoundBodyDrawTest -- docs/VoxelFoundationPlan.md §6.7 "Drawing", F5.3: bodies drawn with one transform.
// Predictions written before the code: docs/evidence/voxfound_f53_prediction.txt (D1-D5).
// Every rig is a hand-built lab shape (FractureLab §8); bodies come from F5.1's extractBodies, motion from F5.2a.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "core/voxfound/BodyDraw.h"
#include "core/voxfound/BodySim.h"
#include "core/voxfound/Bodies.h"
#include "core/voxfound/FractureLab.h"

using namespace Phyxel::Core::VoxFound;

namespace {

Rig rigAt(int k, float e) {
    switch (k) {
        case 0: return rigStoneWall({12, 8, 0}, e);
        case 1: return rigBrickWall(e);
        case 2: return rigBeam(e);
        case 3: return rigGlassPane(e);
        case 4: return rigColumnLintel(e);
        default: return rigChair(e);
    }
}

Extraction extract(Rig& rig, VoxelGrid* worldOut = nullptr) {
    const FractureResult r = fractureReference(rig.grid, rig.hit, rig.mats);
    VoxelGrid w = rig.grid;
    Extraction ex = extractBodies(w, r, rig.hit, rig.mats);
    if (worldOut) *worldOut = w;
    return ex;
}

// D1 for one extraction: every body's faces cover its exposed unit faces exactly and carry its index
void checkFaces(const Extraction& ex, const BodyDrawSet& set, size_t* bodies, size_t* faces) {
    ASSERT_EQ(set.first.size(), ex.bodies.size());
    ASSERT_EQ(set.count.size(), ex.bodies.size());
    EXPECT_EQ(set.refused, 0u);
    uint32_t next = 0;
    for (size_t i = 0; i < ex.bodies.size(); ++i) {
        ASSERT_EQ(set.first[i], next) << "body " << i << ": ranges not contiguous";
        ASSERT_GT(set.count[i], 0u) << "body " << i << " has no faces";
        std::vector<Quad> q;
        for (uint32_t k = set.first[i]; k < set.first[i] + set.count[i]; ++k) {
            ASSERT_EQ(set.faces[k].body, uint32_t(i)) << "face " << k << " carries the wrong body index";
            q.push_back(unpackFace(PackedFace{set.faces[k].a, set.faces[k].b}));
        }
        std::string why;
        ASSERT_EQ(coverageErrors(ex.bodies[i].grid, Outside::Air, q, &why), 0u) << "body " << i << ": " << why;
        next += set.count[i];
    }
    ASSERT_EQ(size_t(next), set.faces.size());
    *bodies += ex.bodies.size();
    *faces += set.faces.size();
}

// a corner's true position: area origin + (body origin + corner voxel) * vm, minus the camera (double)
glm::dvec3 trueCornerRel(const Body& b, glm::ivec3 cornerVox, glm::dvec3 areaOriginM, glm::dvec3 cam) {
    return areaOriginM + glm::dvec3(b.origin + cornerVox) * double(b.grid.voxelM) - cam;
}

// the same corner after the solver moved the body: pos + R (local - comLocal), all double
glm::dvec3 movedCornerRel(const SimBody& s, glm::ivec3 cornerVox, glm::dvec3 areaOriginM, glm::dvec3 cam) {
    const double vm = s.src.grid.voxelM;
    const glm::dvec3 comLocal = (s.src.com - glm::dvec3(s.src.origin)) * vm;
    return areaOriginM + s.pos + s.quat * (glm::dvec3(cornerVox) * vm - comLocal) - cam;
}

std::vector<Body> bodiesOf(const BodySimRef& sim) {
    std::vector<Body> out;
    for (const SimBody& b : sim.bodies()) out.push_back(b.src);
    return out;
}

}  // namespace

// D1: exact faces on every §8 rig over a sweep and under 200 random hits
TEST(VoxFoundBodyDraw, D1FacesAreExactOnEveryRigAndRandomHits) {
    size_t bodies = 0, faces = 0;
    for (int k = 0; k < 6; ++k)
        for (float e : {3.0f, 12.0f, 30.0f, 80.0f}) {
            Rig rig = rigAt(k, e);
            SCOPED_TRACE(rig.name + " E/T " + std::to_string(e));
            const Extraction ex = extract(rig);
            const BodyDrawSet set = buildBodyFaces(ex.bodies);
            checkFaces(ex, set, &bodies, &faces);
            if (HasFatalFailure()) return;
        }
    std::mt19937_64 rng(5301);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    for (int h = 0; h < 200; ++h) {
        Rig rig = rigAt(0, 1.0f);
        size_t i = 0;
        do { i = size_t(U(rng) * double(rig.grid.size())) % rig.grid.size(); } while (!rig.grid.mat[i]);
        const double eot = 0.3 * std::pow(150.0 / 0.3, U(rng));
        rig.hit = {glm::vec3(rig.grid.pos(i)) + 0.5f, {0, 0, 1}, float(eot) * rig.mats[kStone].toughness, rng(), 40.0f};
        SCOPED_TRACE("random hit " + std::to_string(h));
        const Extraction ex = extract(rig);
        const BodyDrawSet set = buildBodyFaces(ex.bodies);
        checkFaces(ex, set, &bodies, &faces);
        if (HasFatalFailure()) return;
    }
    std::printf("[body draw] D1: %zu bodies, %zu faces, all exact\n", bodies, faces);
    EXPECT_GT(bodies, 100u);
}

// D2: faces are built once; 300 ticks of R8b move the bodies and change only their poses
TEST(VoxFoundBodyDraw, D2FacesAreBuiltOnceWhileBodiesMove) {
    Rig rig = rigStoneWall({12, 8, 0}, 80.0f);
    rig.hit.impulse = 8000.0f;
    VoxelGrid world;
    const Extraction ex = extract(rig, &world);
    ASSERT_GT(ex.bodies.size(), 5u);
    BodySimRef sim(world, rig.mats, BodySimParams{});
    ASSERT_TRUE(sim.implemented());
    for (const Body& b : ex.bodies) sim.add(b, b.vel, b.angVel);
    const size_t builds0 = bodyFaceBuildCount();
    const BodyDrawSet set = buildBodyFaces(sim.bodies());
    ASSERT_FALSE(set.faces.empty());
    const uint64_t h0 = set.hash();
    ASSERT_NE(h0, 0u);
    std::vector<BodyPose> poses;
    double moved = 0.0;
    const glm::dvec3 start = sim.bodies()[0].pos;
    for (int t = 0; t < 300; ++t) {
        sim.step();
        poses.clear();
        for (const SimBody& b : sim.bodies()) poses.push_back(poseOf(b, glm::dvec3(0.0), glm::dvec3(1.5, 1.0, 4.0)));
        ASSERT_EQ(poses.size(), set.first.size());
        moved = std::max(moved, glm::length(sim.bodies()[0].pos - start));
    }
    EXPECT_EQ(set.hash(), h0);
    EXPECT_EQ(bodyFaceBuildCount() - builds0, 1u) << "the faces were rebuilt during motion";
    EXPECT_GT(moved, 0.05) << "the rig did not move (the test would prove nothing)";
    std::printf("[body draw] D2: %zu bodies, %zu faces built once; body 0 moved %.2f m over 300 ticks\n",
                set.first.size(), set.faces.size(), moved);
}

// D3: before the first step the transform is the identity, exactly
TEST(VoxFoundBodyDraw, D3IdentityPoseIsExact) {
    Rig rig = rigStoneWall({12, 8, 0}, 30.0f);
    VoxelGrid world;
    const Extraction ex = extract(rig, &world);
    ASSERT_GT(ex.bodies.size(), 0u);
    BodySimRef sim(world, rig.mats, BodySimParams{});
    for (const Body& b : ex.bodies) sim.add(b, b.vel, b.angVel);
    const BodyDrawSet set = buildBodyFaces(sim.bodies());
    ASSERT_EQ(set.first.size(), sim.bodies().size());
    const glm::dvec3 area(-40.0, 17.0, 260.0);
    double worstWhole = 0.0, worstAny = 0.0;
    size_t corners = 0;
    for (const glm::dvec3 cam : {glm::dvec3(-36.0, 18.0, 266.0), glm::dvec3(-38.3172, 18.913, 263.0071)}) {
        const bool whole = cam == glm::floor(cam);
        for (size_t i = 0; i < sim.bodies().size(); ++i) {
            const BodyPose p = poseOf(sim.bodies()[i], area, cam);
            for (uint32_t k = set.first[i]; k < set.first[i] + set.count[i]; ++k)
                for (uint32_t c = 0; c < 4; ++c) {
                    const PackedFace f{set.faces[k].a, set.faces[k].b};
                    const glm::dvec3 got(bodyCornerRel(p, f, c));
                    const glm::dvec3 want = trueCornerRel(sim.bodies()[i].src, faceCornerVoxel(f, c), area, cam);
                    const double err = glm::length(got - want);
                    (whole ? worstWhole : worstAny) = std::max(whole ? worstWhole : worstAny, err);
                    ++corners;
                }
        }
    }
    std::printf("[body draw] D3: %zu corners; worst %.3g m (whole-metre camera), %.3g m (any camera)\n", corners,
                worstWhole, worstAny);
    EXPECT_EQ(worstWhole, 0.0);
    EXPECT_LE(worstAny, 1e-6);
    // corners must land on faces' own geometry: one known face of a known body, by hand
    // +Y at y = 4; u = (axis+1)%3 = z spans du = 2 (z 1..3), v = x spans dv = 1 (x 2..3). (Run 1 had x and z swapped
    // in this hand expectation -- a test error, the code was right; fixed after run 1, recorded.)
    const PackedFace f = packFace(Quad{{2, 3, 1}, 1, 1, 2, 1, kStone});
    glm::ivec3 lo(1000), hi(-1000);
    for (uint32_t c = 0; c < 4; ++c) {
        lo = glm::min(lo, faceCornerVoxel(f, c));
        hi = glm::max(hi, faceCornerVoxel(f, c));
    }
    EXPECT_EQ(lo, glm::ivec3(2, 4, 1));
    EXPECT_EQ(hi, glm::ivec3(3, 4, 3));
}

// D4: after the solver moved and turned the bodies, the float transform equals the double one; normals rotate
TEST(VoxFoundBodyDraw, D4MovedPoseMatchesDouble) {
    Rig rig = rigStoneWall({12, 8, 0}, 80.0f);
    rig.hit.impulse = 8000.0f;
    VoxelGrid world;
    const Extraction ex = extract(rig, &world);
    BodySimRef sim(world, rig.mats, BodySimParams{});
    for (const Body& b : ex.bodies) sim.add(b, b.vel, b.angVel);
    const BodyDrawSet set = buildBodyFaces(sim.bodies());
    for (int t = 0; t < 300; ++t) sim.step();
    const glm::dvec3 area(-40.0, 17.0, 260.0), cam(-38.3172, 18.913, 263.0071);
    double worst = 0.0, worstN = 0.0, worstUnit = 0.0, maxAngle = 0.0;
    for (size_t i = 0; i < sim.bodies().size(); ++i) {
        const SimBody& s = sim.bodies()[i];
        maxAngle = std::max(maxAngle, 2.0 * std::acos(std::min(1.0, std::abs(s.quat.w))));
        const BodyPose p = poseOf(s, area, cam);
        for (uint32_t k = set.first[i]; k < set.first[i] + set.count[i]; ++k) {
            const PackedFace f{set.faces[k].a, set.faces[k].b};
            for (uint32_t c = 0; c < 4; ++c)
                worst = std::max(worst, glm::length(glm::dvec3(bodyCornerRel(p, f, c)) -
                                                    movedCornerRel(s, faceCornerVoxel(f, c), area, cam)));
            const Quad q = unpackFace(f);
            glm::dvec3 n(0.0);
            n[q.axis] = q.dir;
            const glm::dvec3 got(bodyNormal(p, f));
            worstN = std::max(worstN, glm::length(got - s.quat * n));
            worstUnit = std::max(worstUnit, std::abs(glm::length(got) - 1.0));
        }
    }
    std::printf("[body draw] D4: %zu bodies turned up to %.1f deg; worst corner %.3g m, normal %.3g, |n|-1 %.3g\n",
                sim.bodies().size(), maxAngle * 57.29578, worst, worstN, worstUnit);
    EXPECT_GT(maxAngle, 0.05) << "no body turned (the test would prove nothing)";
    EXPECT_LE(worst, 3e-6);
    EXPECT_LE(worstN, 1e-6);
    EXPECT_LE(worstUnit, 1e-6);
}

// D5: cost on the F5.1 large wall (reported; the prediction is 2-15 ms for ~950 bodies)
TEST(VoxFoundBodyDraw, D5CostOnALargeWall) {
    VoxelGrid g;
    g.resize({256, 128, 4});
    g.voxelM = 0.125f;
    for (int x = 0; x < 256; ++x)
        for (int y = 0; y < 128; ++y)
            for (int z = 0; z < 4; ++z) g.set({x, y, z}, kStone, 0, y == 0);
    const auto mats = labMaterials();
    for (float e : {1000.0f, 2500.0f}) {
        const Hit hit{{128, 64, 0}, {0, 0, 1}, e * mats[kStone].toughness, 777, 200.0f};
        const FractureResult r = fractureReference(g, hit, mats);
        VoxelGrid w = g;
        const Extraction ex = extractBodies(w, r, hit, mats);
        double best = 1e30;
        BodyDrawSet set;
        for (int rep = 0; rep < 5; ++rep) {
            const auto t0 = std::chrono::steady_clock::now();
            set = buildBodyFaces(ex.bodies);
            best = std::min(best, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        size_t b = 0, f = 0;
        checkFaces(ex, set, &b, &f);
        if (HasFatalFailure()) return;
        std::vector<uint32_t> per(set.count);
        std::sort(per.begin(), per.end());
        BodySimRef sim(w, mats, BodySimParams{});
        for (const Body& body : ex.bodies) sim.add(body, body.vel, body.angVel);
        std::vector<BodyPose> poses(sim.bodies().size());
        double poseBest = 1e30;
        for (int rep = 0; rep < 5; ++rep) {
            const auto t0 = std::chrono::steady_clock::now();
            for (size_t i = 0; i < poses.size(); ++i) poses[i] = poseOf(sim.bodies()[i], {}, {3.0, 2.0, 9.0});
            poseBest = std::min(poseBest,
                                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        std::printf("[body draw] D5 large wall E/T %.0f: %zu bodies, %zu faces (%zu B), faces per body median %u max "
                    "%u; build %.2f ms, poses %.4f ms (best of 5)\n",
                    e, ex.bodies.size(), set.faces.size(), set.faces.size() * sizeof(BodyFace),
                    per.empty() ? 0u : per[per.size() / 2], per.empty() ? 0u : per.back(), best, poseBest);
    }
}
