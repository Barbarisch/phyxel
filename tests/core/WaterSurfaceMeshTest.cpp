#include <gtest/gtest.h>

#include "core/water/WaterSurfaceMesh.h"

#include <cmath>
#include <map>

// WaterCore Phase F (docs/WaterCore.md 17.2 key 5): the surface mesh is a pure function of the
// volume's surface field. Top vertices equal the sub-column surface; lateral faces appear only
// where water meets air; two abutting volumes mesh exactly like one; the render default is the mesh.

namespace Phyxel::Core::Water {
namespace {

struct Tank {
    WaterGrid grid;
    Tank(int nx, int ny, int nz, float h) : grid(GridSpec{glm::ivec3(0), glm::ivec3(nx, ny, nz), h}) {}
};

// a 6x6 (1/3 m) pool: layers 0..2 full, layer 3 at 0.4 (surface 3.4/3 = 1.1333 m), with a solid
// wall along x = 0 (occ) and a step: columns x >= 4 hold water only to layer 1 (a 2-cell drop)
Tank steppedPool() {
    Tank t(6, 8, 6, 1.0f / 3.0f);
    for (int z = 0; z < 6; ++z) for (int x = 0; x < 6; ++x) {
        if (x == 0) { for (int y = 0; y < 8; ++y) t.grid.occ(x, y, z) = Occ::Solid; continue; }
        const int topFull = x >= 4 ? 0 : 2;
        for (int y = 0; y <= topFull; ++y) t.grid.f(x, y, z) = 1.0f;
        t.grid.f(x, topFull + 1, z) = 0.4f;
    }
    return t;
}

TEST(WaterSurfaceMeshTest, TopEqualsSurfaceEverywhere) {
    Tank t = steppedPool();
    WaterSurfaceField f; extractSurfaceField(t.grid, f);
    ASSERT_EQ(f.nx, 6); ASSERT_EQ(f.nz, 6);
    for (int z = 0; z < 6; ++z) for (int x = 1; x < 6; ++x) {
        const SurfaceColumn& c = f.at(x, z);
        ASSERT_EQ(static_cast<int>(c.runs), 1) << x << "," << z;
        EXPECT_NEAR(c.top[0], t.grid.surfaceWorldY(x, z), 1e-6f) << "the field's top IS the solver's surface";
        EXPECT_NEAR(c.bottom[0], 0.0f, 1e-6f);
    }
    EXPECT_EQ(static_cast<int>(f.at(0, 0).runs), 0);
    EXPECT_NEAR(f.at(0, 0).solidTopY, 8.0f / 3.0f, 1e-6f) << "the wall column reports its top face";
    WaterSurfaceMesh m; buildWaterSurfaceMesh(f, m);
    // every top vertex is the MEAN of the surfaces of the wet sub-columns sharing its corner (all one
    // body here: every run starts at the floor), within 1 mm - exactly the surface away from the
    // step, the mean of 1.133 and 0.467 on the step's edge (a height field bends a one-body step
    // across one cell; it never floats above or sinks below the water on either side)
    for (const auto& v : m.vertices) {
        if (v.side != 0.0f) continue;
        const int cx = static_cast<int>(std::floor(v.pos.x / f.h + 0.5f)), cz = static_cast<int>(std::floor(v.pos.z / f.h + 0.5f));
        float sum = 0.0f, lo = 1e9f, hi = -1e9f; int n = 0;
        for (int dz = -1; dz <= 0; ++dz) for (int dx = -1; dx <= 0; ++dx) {
            const int x = cx + dx, z = cz + dz;
            if (x < 1 || z < 0 || x >= 6 || z >= 6) continue;
            const float sy = t.grid.surfaceWorldY(x, z);
            sum += sy; ++n; lo = std::min(lo, sy); hi = std::max(hi, sy);
        }
        ASSERT_GT(n, 0);
        EXPECT_NEAR(v.pos.y, sum / n, 1e-3f) << "vertex at " << v.pos.x << "," << v.pos.z;
        EXPECT_GE(v.pos.y, lo - 1e-6f); EXPECT_LE(v.pos.y, hi + 1e-6f);
    }
    EXPECT_EQ(m.topQuads, 30) << "one top quad per wet sub-column";
}

TEST(WaterSurfaceMeshTest, LateralFacesOnlyAtWaterEdges) {
    Tank t = steppedPool();
    WaterSurfaceField f; extractSurfaceField(t.grid, f);
    WaterSurfaceMesh m; buildWaterSurfaceMesh(f, m);
    // the step between x = 3 (surface 1.133) and x = 4 (surface 0.467): the runs still OVERLAP in Y
    // (both start at 0), so the surface is shared and bends - no lateral face; the wall at x = 0 is
    // solid above the water - no face; the tank border is the volume's edge - no face. Result: zero
    // lateral faces for a pool that only meets walls and its own water.
    EXPECT_EQ(m.sideQuads, 0);
    // now an OVERHANG: a second pool on a shelf at layer 5 over columns x 4..5 (air below it)
    for (int z = 0; z < 6; ++z) for (int x = 4; x < 6; ++x) { t.grid.f(x, 5, z) = 1.0f; t.grid.f(x, 6, z) = 0.5f; }
    extractSurfaceField(t.grid, f);
    EXPECT_EQ(static_cast<int>(f.at(5, 0).runs), 2) << "two runs: the pool and the shelf water";
    WaterSurfaceMesh m2; buildWaterSurfaceMesh(f, m2);
    // the shelf water's west edge (x = 4 against x = 3, whose run tops at 1.133 < the shelf's bottom
    // 1.667) is water meeting air: a lateral face per z row, down to the shelf run's own bottom
    EXPECT_EQ(m2.sideQuads, 6);
    for (const auto& v : m2.vertices) if (v.side == 1.0f) { EXPECT_NEAR(v.pos.x, 4.0f / 3.0f, 1e-6f); EXPECT_GE(v.pos.y, 5.0f / 3.0f - 1e-6f); }
}

TEST(WaterSurfaceMeshTest, TwoAbuttingVolumesMeshLikeOne) {
    // 14.2 shape: the same pool as one 6x6 volume and as two 3x6 volumes side by side; every
    // vertex of the whole-volume mesh exists in the union of the two half meshes at the same height,
    // and the halves introduce no lateral face at their shared edge (the water continues)
    Tank whole = steppedPool();
    WaterSurfaceField fw; extractSurfaceField(whole.grid, fw);
    WaterSurfaceMesh mw; buildWaterSurfaceMesh(fw, mw);
    std::map<std::pair<int, int>, float> heights;   // (x, z) in 1/3 m lattice units of corners -> y
    for (const auto& v : mw.vertices) if (v.side == 0.0f) heights[{static_cast<int>(std::lround(v.pos.x * 3.0f)), static_cast<int>(std::lround(v.pos.z * 3.0f))}] = v.pos.y;
    WaterSurfaceMesh mh;
    for (int half = 0; half < 2; ++half) {
        WaterGrid g(GridSpec{glm::ivec3(half * 3, 0, 0), glm::ivec3(3, 8, 6), 1.0f / 3.0f});
        for (int z = 0; z < 6; ++z) for (int y = 0; y < 8; ++y) for (int x = 0; x < 3; ++x) {
            g.f(x, y, z) = whole.grid.f(half * 3 + x, y, z);
            g.occ(x, y, z) = whole.grid.occ(half * 3 + x, y, z);
        }
        WaterSurfaceField fh; extractSurfaceField(g, fh);
        buildWaterSurfaceMesh(fh, mh);
    }
    EXPECT_EQ(mh.sideQuads, 0) << "no edge at the seam";
    EXPECT_EQ(mh.topQuads, mw.topQuads);
    // interior corners away from the seam agree exactly; at the seam each half averages fewer
    // neighbours, so the seam column's corners may differ: report the max difference, gate it at the
    // cell's fill step (the halves cannot see across the seam - the gap the span grid would show)
    float maxSeam = 0.0f, maxAway = 0.0f;
    for (const auto& v : mh.vertices) {
        if (v.side != 0.0f) continue;
        const int kx = static_cast<int>(std::lround(v.pos.x * 3.0f)), kz = static_cast<int>(std::lround(v.pos.z * 3.0f));
        const auto it = heights.find({kx, kz});
        ASSERT_NE(it, heights.end());
        const float d = std::abs(it->second - v.pos.y);
        if (kx == 3) maxSeam = std::max(maxSeam, d); else maxAway = std::max(maxAway, d);
    }
    EXPECT_LT(maxAway, 1e-6f) << "away from the seam the two meshes are the same mesh";
    RecordProperty("seam_max_mm", maxSeam * 1000.0f);
    EXPECT_LT(maxSeam, 1e-6f) << "the pool is flat across x = 3: the seam corners agree too";
}

TEST(WaterSurfaceMeshTest, NormalsPointOutOfTheWater) {
    // the Phase F taps found every pixel at fresnel 1: the quad normal came out -y (a winding
    // artefact), the shader read the whole pond as grazing and painted the sky white
    Tank t = steppedPool();
    for (int z = 0; z < 6; ++z) for (int x = 4; x < 6; ++x) { t.grid.f(x, 5, z) = 1.0f; t.grid.f(x, 6, z) = 0.5f; }
    WaterSurfaceField f; extractSurfaceField(t.grid, f);
    WaterSurfaceMesh m; buildWaterSurfaceMesh(f, m);
    int tops = 0, sides = 0;
    for (const auto& v : m.vertices) {
        if (v.side == 0.0f) { EXPECT_GT(v.normal.y, 0.5f) << "top normal must point up"; ++tops; }
        else { EXPECT_NEAR(v.normal.y, 0.0f, 1e-6f); EXPECT_LT(v.normal.x, -0.99f) << "the shelf's west edge faces -x"; ++sides; }
        EXPECT_NEAR(glm::length(v.normal), 1.0f, 1e-5f);
    }
    EXPECT_GT(tops, 0); EXPECT_GT(sides, 0);
}

TEST(WaterSurfaceMeshTest, FoamAndFlowReachTheTopRunsVertices) {
    // G2: a 3 x 3 field, every column one run 0..1; the centre column foams (1) and flows (+0.5, 0):
    // its top quad's four vertices carry exactly that, the neighbours' carry zero, and a second
    // (lower) run in the centre carries none (foam lives on the free surface)
    WaterSurfaceField f; f.origin = glm::ivec3(0); f.nx = 3; f.nz = 3; f.h = 1.0f;
    f.cols.assign(9, SurfaceColumn{});
    for (auto& c : f.cols) { c.runs = 1.0f; c.bottom[0] = 0.0f; c.top[0] = 1.0f; }
    SurfaceColumn& m = f.cols[4];
    m.runs = 2.0f; m.bottom[0] = -2.0f; m.top[0] = -1.0f; m.bottom[1] = 0.0f; m.top[1] = 1.0f;
    m.foam = 1.0f; m.u = 0.5f; m.w = 0.0f;
    WaterSurfaceMesh mesh; buildWaterSurfaceMesh(f, mesh);
    // quads come in fours; classify each TOP quad by its centroid (a vertex on a shared edge belongs to two columns)
    int centreTop = 0, centreLow = 0, others = 0;
    ASSERT_EQ(mesh.vertices.size() % 4, 0u);
    for (size_t q = 0; q < mesh.vertices.size(); q += 4) {
        if (mesh.vertices[q].side != 0.0f) continue;
        glm::vec3 cen(0.0f);
        for (size_t k = 0; k < 4; ++k) cen += mesh.vertices[q + k].pos;
        cen *= 0.25f;
        const bool centre = cen.x > 1.0f && cen.x < 2.0f && cen.z > 1.0f && cen.z < 2.0f;
        for (size_t k = 0; k < 4; ++k) {
            const auto& v = mesh.vertices[q + k];
            if (centre && cen.y > 0.5f) { EXPECT_FLOAT_EQ(v.foam, 1.0f); EXPECT_FLOAT_EQ(v.flow.x, 0.5f); EXPECT_FLOAT_EQ(v.flow.y, 0.0f); }
            else { EXPECT_FLOAT_EQ(v.foam, 0.0f); EXPECT_FLOAT_EQ(v.flow.x, 0.0f); }
        }
        if (centre && cen.y > 0.5f) ++centreTop; else if (centre) ++centreLow; else ++others;
    }
    EXPECT_EQ(centreTop, 1); EXPECT_EQ(centreLow, 1); EXPECT_EQ(others, 8);
    EXPECT_EQ(sizeof(WaterSurfaceVertex), 48u);
}

TEST(WaterSurfaceMeshTest, DefaultModeIsMesh) {
    EXPECT_EQ(kWaterCoreRenderModeDefault, WaterCoreRenderMode::Mesh);
    EXPECT_STREQ(waterCoreRenderModeName(kWaterCoreRenderModeDefault), "mesh");
}

}  // namespace
}  // namespace Phyxel::Core::Water
