// WaterCore.md 58.11 - the edge blend's rules (EdgeBlend.h; water_surface.vert's edgeBlendLift is its line-for-line copy).
#include <gtest/gtest.h>
#include "core/water/EdgeBlend.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Phyxel::Core::Water;

namespace {
// a 40 m live box at x 0..40, z 0..40; the volume draws `inside` where `drawn(x, z)` says, nothing elsewhere
template <class F> EdgeProfile grid(float inside, F drawn) {
    EdgeProfile p; p.lo = glm::ivec2(0); p.nx = 40; p.nz = 40; p.tops.assign(1600, kEdgeDry);
    for (int z = 0; z < 40; ++z) for (int x = 0; x < 40; ++x) if (drawn(x, z)) p.tops[static_cast<size_t>(x + 40 * z)] = inside;
    return p;
}
}  // namespace

// The seam closes at a box's OUTER edge: the record drawn right at the edge meets the volume's level, by M it is the record's own,
// and between no jump of its own. RED: M = 0 (off) - the full step. The three seams' sizes (58.4: 1 cm; 58.5: 12 cm; 58.9: 16 cm).
TEST(WaterEdgeBlend, TheRecordMeetsTheVolumesEdgeAndReturnsByM) {
    for (float step : {0.01f, 0.12f, 0.16f}) {
        const float record = 2.0f, inside = record + step;
        const std::vector<EdgeProfile> b{grid(inside, [](int, int) { return true; })};
        auto drawn = [&](float x, float M) { return record + edgeBlendLift(glm::vec3(x, record, 20.5f), b, M); };
        const float atEdge = drawn(40.0f + 1e-4f, 3.0f), atM = drawn(43.0f, 3.0f), off = drawn(40.0f + 1e-4f, 0.0f);
        float prev = atEdge, worst = 0.0f;
        for (float x = 40.05f; x <= 43.0f; x += 0.05f) { const float y = drawn(x, 3.0f); worst = std::max(worst, std::abs(y - prev)); prev = y; }
        std::printf("  outer edge, step %.2f m: at the edge %.4f (the volume %.4f), at M %.4f (the record %.4f), largest jump over 5 cm %.4f | RED (off) %.4f\n",
                    step, atEdge, inside, atM, record, worst, off);
        EXPECT_NEAR(atEdge, inside, 1e-3f);
        EXPECT_NEAR(atM, record, 1e-5f);
        EXPECT_LT(worst, 0.15f * step + 1e-4f);
        EXPECT_NEAR(off, record, 1e-6f);
    }
}

// 58.4's seam: INSIDE a patch's box, its awake water (x < 20) against its own recorded tiles (x >= 20, drawn by the record). The
// record's vertex beside the awake region is lifted toward it; 3 m in, the record's own level. (The side-profile first cut gave 0
// here - every vertex inside a box was left alone - and the live line stayed.)
TEST(WaterEdgeBlend, ThePatchsInnerBoundaryBlendsToo) {
    const float record = 90.0f, awake = 89.99f;
    const std::vector<EdgeProfile> b{grid(awake, [](int x, int) { return x < 20; })};
    const float at = record + edgeBlendLift(glm::vec3(20.0f + 1e-4f, record, 20.5f), b, 3.0f);
    const float far = record + edgeBlendLift(glm::vec3(23.5f, record, 20.5f), b, 3.0f);
    std::printf("  inner boundary: the record at it %.4f (the awake water %.4f), 3.5 m in %.4f (the record %.4f)\n", at, awake, far, record);
    EXPECT_NEAR(at, awake, 1e-3f);
    EXPECT_NEAR(far, record, 1e-5f);
}

// Dry cells lift nothing; a record vertex far from any drawn cell is untouched; two volumes: the nearest drawn cell wins
TEST(WaterEdgeBlend, DryAndTwoVolumes) {
    EdgeProfile a = grid(2.1f, [](int, int) { return true; });
    EdgeProfile none = grid(2.1f, [](int, int) { return false; });
    EXPECT_EQ(edgeBlendLift(glm::vec3(41.0f, 2.0f, 20.0f), {none}, 3.0f), 0.0f);
    EXPECT_EQ(edgeBlendLift(glm::vec3(60.0f, 2.0f, 20.0f), {a}, 3.0f), 0.0f);
    EdgeProfile c = grid(1.9f, [](int, int) { return true; }); c.lo = glm::ivec2(44, 0);
    EXPECT_GT(edgeBlendLift(glm::vec3(40.5f, 2.0f, 20.0f), {a, c}, 3.0f), 0.0f);
    EXPECT_LT(edgeBlendLift(glm::vec3(43.5f, 2.0f, 20.0f), {a, c}, 3.0f), 0.0f);
}

// 58.11 - the volume's own mesh at a recorded neighbour: with the edge blend on, its corners keep the field's own top (the record is
// lifted to meet it); off, they take the record's level (27.15 - the 1 cm facet the blend could not reach, live). A field 20 x 4
// columns: x < 10 the volume's water at 1.99, x >= 10 a recorded tile (runs -1, its level 2.0). The top vertices at x = 10 h.
#include "core/water/WaterSurfaceMesh.h"
TEST(WaterEdgeBlend, TheVolumesCornersKeepTheirOwnTopWithTheBlend) {
    WaterSurfaceField f; f.nx = 20; f.nz = 4; f.h = 1.0f / 9.0f; f.origin = glm::ivec3(0); f.cols.resize(80);
    for (int z = 0; z < 4; ++z) for (int x = 0; x < 20; ++x) {
        SurfaceColumn& c = f.cols[static_cast<size_t>(x + 20 * z)];
        if (x < 10) { c.runs = 1.0f; c.bottom[0] = 1.0f; c.top[0] = 1.99f; }
        else { c.runs = -1.0f; c.bottom[0] = 1.0f; c.top[0] = 2.0f; }
    }
    auto edgeY = [&](bool on) {
        s_meshOwnTopAtRecord = on;
        WaterSurfaceMesh m; buildWaterSurfaceMesh(f, m);
        s_meshOwnTopAtRecord = false;
        float y = 0.0f; int n = 0;
        for (const auto& v : m.vertices) if (v.side < 0.5f && std::abs(v.pos.x - 10.0f * f.h) < 1e-4f) { y += v.pos.y; ++n; }
        return n ? y / n : -1.0f;
    };
    const float off = edgeY(false), on = edgeY(true);
    std::printf("  the volume's top at the recorded neighbour: off (27.15) %.4f, on (the blend) %.4f\n", off, on);
    EXPECT_NEAR(off, 2.0f, 1e-4f) << "RED: off takes the record's level";
    EXPECT_NEAR(on, 1.99f, 1e-4f) << "the corner did not keep the field's own top";
}
