// docs/VoxelFoundationPlan.md F7.20 -- road grading on the 1/8 m terrain (docs/evidence/voxfound_f7_prediction.txt G1-G3).
// World: RoadFieldSeamTest's canonical WorldForge test world (Perlin 20260816, 3 sites, 768 u region).
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/WorldForgePlan.h"
#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"
#include "core/voxfound/StorageLab.h"

using namespace Phyxel;
using namespace Phyxel::Core::VoxFound;

namespace {

WorldRecipe forgeRecipe(WorldGenerator& gen) {
    WorldRecipe r = gen.makeRecipe();
    r.worldforge.enabled = true;
    r.worldforge.siteCount = 3;
    r.worldforge.regionRadius = 768.0f;
    r.worldforge.minSpacing = 256.0f;
    return r;
}

struct Forge {
    WorldGenerator gen{WorldGenerator::GenerationType::Perlin, 20260816};
    Forge() { gen.applyRecipe(forgeRecipe(gen)); }
};

// The top face (metres) of 1/8 m voxel column (vx, vz) of the area: the highest solid voxel + 1.
float topFace(const VoxelArea& a, int vx, int vz) {
    for (int vy = a.grid.dim.y - 1; vy >= 0; --vy)
        if (a.grid.at({vx, vy, vz})) return float(a.originVoxel.y + vy + 1) / float(kVoxelsPerM);
    return -1e30f;
}

struct G1 {
    int vertices = 0, columns = 0, outside = 0, ford = 0;
    float worst = 0.0f;
};

G1 measureG1(WorldGenerator& gen, float tol) {
    G1 g;
    const WorldForgePlan* plan = gen.worldForge();
    for (const auto& road : plan->roads())
        for (size_t k = 0; k < road.centerline.size(); k += 4) {
            const glm::vec2 v = road.centerline[k];
            const int mx = int(std::floor(v.x)), mz = int(std::floor(v.y));
            const auto col = gen.sampleSurface(mx, mz);
            if (!col.graded || col.roadClass == 0) continue;   // wet / bridged / not stamped: not graded here
            const auto hit = plan->roadAt(v.x, v.y);
            if (hit.gradeY < -1e29f) continue;
            const float want = hit.gradeY + 1.0f;   // today's graded top face
            const int lo = int(std::floor(std::min(want, col.bedHeight))) - 4;
            const int hi = int(std::ceil(std::max(want, col.bedHeight + 1.0f))) + 4;
            const VoxelArea a = buildTerrainArea(gen, {mx - 1, lo, mz - 1}, {3, hi - lo, 3});
            ++g.vertices;
            for (int vx = 0; vx < a.grid.dim.x; ++vx)
                for (int vz = 0; vz < a.grid.dim.z; ++vz) {
                    const float wx = (a.originVoxel.x + vx + 0.5f) / kVoxelsPerM, wz = (a.originVoxel.z + vz + 0.5f) / kVoxelsPerM;
                    if (std::hypot(wx - v.x, wz - v.y) > 0.5f) continue;
                    {   // the contract is "where today grades": all four bilinear metre samples graded (a creek ford --
                        // order 1-2, riverOrder > 0 -- is never graded, today or here)
                        const int ix = int(std::floor(wx - 0.5f)), iz = int(std::floor(wz - 0.5f));
                        bool all = true;
                        for (int dx = 0; dx < 2; ++dx)
                            for (int dz = 0; dz < 2; ++dz) all = all && gen.sampleSurface(ix + dx, iz + dz).graded;
                        if (!all) { ++g.ford; continue; }
                    }
                    ++g.columns;
                    const float d = std::fabs(topFace(a, vx, vz) - want);
                    g.worst = std::max(g.worst, d);
                    if (d > tol) {
                        ++g.outside;
                        const int ix = int(std::floor(wx - 0.5f)), iz = int(std::floor(wz - 0.5f));
                        std::printf("  outside: vertex (%.2f,%.2f) col (%.3f,%.3f) top %.3f want %.3f |", v.x, v.y, wx, wz,
                                    topFace(a, vx, vz), want);
                        for (int dx = 0; dx < 2; ++dx)
                            for (int dz = 0; dz < 2; ++dz) {
                                const auto c = gen.sampleSurface(ix + dx, iz + dz);
                                const auto h = plan->roadAt(float(ix + dx) + 0.5f, float(iz + dz) + 0.5f);
                                std::printf(" [%d,%d g%d gh %.2f bed %.2f river %d grade %.2f dist %.2f]", ix + dx, iz + dz,
                                            int(c.graded), c.gradedHeight, c.bedHeight, c.riverOrder, h.gradeY, h.dist);
                            }
                        std::printf("\n");
                    }
                }
        }
    return g;
}

}  // namespace

TEST(VoxFoundRoadGrade, G1_TopFaceFollowsTheGrade) {
    Forge w;
    ASSERT_NE(w.gen.worldForge(), nullptr);
    ASSERT_FALSE(w.gen.worldForge()->roads().empty());
    const float tol = 1.0f / kVoxelsPerM + WorldForgePlan::kMaxRoadGrade * 0.5f;
    setRoadGradeStub(true);   // the red: today's 1/8 m terrain (bedHeight everywhere)
    const G1 red = measureG1(w.gen, tol);
    setRoadGradeStub(false);
    const G1 green = measureG1(w.gen, tol);
    std::printf("[F7.20 G1] tol %.3f m | red: %d vertices %d columns, %d outside, worst %.3f m | green: %d vertices %d "
                "columns, %d outside, worst %.3f m\n",
                tol, red.vertices, red.columns, red.outside, red.worst, green.vertices, green.columns, green.outside,
                green.worst);
    std::printf("[F7.20 G1] columns next to an ungraded (ford) metre, excluded: %d\n", green.ford);
    ASSERT_GT(green.vertices, 20) << "too few graded vertices to say anything";
    EXPECT_GT(red.outside, 0) << "the red must show the natural bumps";
    EXPECT_EQ(green.outside, 0);
}

TEST(VoxFoundRoadGrade, G2_OffRoadAreasAreByteIdentical) {
    Forge w;
    const WorldForgePlan* plan = w.gen.worldForge();
    int checked = 0;
    for (int i = 0; i < 40 && checked < 4; ++i) {
        const int x = -600 + i * 37, z = 500 - i * 29;
        bool clear = true;
        for (int dx = -8; dx <= 24 && clear; dx += 4)
            for (int dz = -8; dz <= 24 && clear; dz += 4) {
                const auto h = plan->roadAt(float(x + dx), float(z + dz));
                if (h.cls > 0 && h.dist <= WorldForgePlan::roadHalfWidth(h.cls) + 8.0f) clear = false;
            }
        if (!clear) continue;
        const int y0 = w.gen.sampleSurface(x + 8, z + 8).surfaceY - 8;
        setRoadGradeStub(true);
        const VoxelArea a = buildTerrainArea(w.gen, {x, y0, z}, {16, 16, 16});
        setRoadGradeStub(false);
        const VoxelArea b = buildTerrainArea(w.gen, {x, y0, z}, {16, 16, 16});
        ASSERT_EQ(a.palette, b.palette);
        EXPECT_TRUE(a.grid.mat == b.grid.mat) << "off-road area at " << x << "," << z << " changed";
        ++checked;
    }
    EXPECT_GE(checked, 3);
}

TEST(VoxFoundRoadGrade, G3_ChunkedEqualsWholeAcrossARoad) {
    Forge w;
    const WorldForgePlan* plan = w.gen.worldForge();
    const auto& cl = plan->roads().front().centerline;
    const glm::vec2 v = cl[cl.size() / 2];
    const int x0 = int(std::floor(v.x / 32.0f)) * 32 - 16, z0 = int(std::floor(v.y)) - 8;
    int lo = 1 << 30, hi = -(1 << 30);
    for (int x = x0 - 1; x <= x0 + 32; ++x)
        for (int z = z0 - 1; z <= z0 + 16; ++z) {
            const auto c = w.gen.sampleSurface(x, z);
            const float g = c.graded ? c.gradedHeight : c.bedHeight;
            lo = std::min(lo, int(std::floor(g)) - 3);
            hi = std::max(hi, int(std::ceil(g)) + 3);
        }
    const VoxelArea whole = buildTerrainArea(w.gen, {x0, lo, z0}, {32, hi - lo, 16});
    const VoxelArea left = buildTerrainArea(w.gen, {x0, lo, z0}, {16, hi - lo, 16});
    const VoxelArea right = buildTerrainArea(w.gen, {x0 + 16, lo, z0}, {16, hi - lo, 16});
    size_t bad = 0;
    auto name = [](const VoxelArea& a, uint8_t m) { return a.palette[m]; };
    for (int vx = 0; vx < whole.grid.dim.x; ++vx)
        for (int vy = 0; vy < whole.grid.dim.y; ++vy)
            for (int vz = 0; vz < whole.grid.dim.z; ++vz) {
                const VoxelArea& part = vx < left.grid.dim.x ? left : right;
                const int px = vx < left.grid.dim.x ? vx : vx - left.grid.dim.x;
                bad += name(whole, whole.grid.at({vx, vy, vz})) != name(part, part.grid.at({px, vy, vz}));
            }
    EXPECT_EQ(bad, 0u);
}
