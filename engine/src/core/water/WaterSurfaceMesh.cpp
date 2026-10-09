#include "core/water/WaterSurfaceMesh.h"

#include <algorithm>
#include <cmath>

namespace Phyxel::Core::Water {

void extractSurfaceField(const WaterGrid& g, WaterSurfaceField& out) {
    const GridSpec& sp = g.spec();
    out.origin = sp.origin; out.nx = g.nx(); out.nz = g.nz(); out.h = sp.h;
    out.cols.assign(static_cast<size_t>(out.nx) * out.nz, SurfaceColumn{});
    const float h = sp.h;
    const float fMin = WaterGrid::kSurfaceMinDepth / h;
    for (int z = 0; z < g.nz(); ++z)
        for (int x = 0; x < g.nx(); ++x) {
            SurfaceColumn& c = out.cols[static_cast<size_t>(x) + static_cast<size_t>(out.nx) * z];
            int runs = 0;
            int topCell = -1;   // the top run's top cell (E2)
            int y = 0;
            while (y < g.ny()) {
                if (g.occ(x, y, z) == Occ::Solid) c.solidTopY = static_cast<float>(sp.origin.y + y + 1) * h;
                if (g.f(x, y, z) < fMin || g.occ(x, y, z) == Occ::Solid) { ++y; continue; }
                const int start = y;
                int top = y;
                // the HEIGHT FUNCTION (docs/WaterCore.md 19.5): the run's surface is its bottom plus the
                // water actually in it (sum of f), not the fill line of its highest wet cell - a few mm of
                // film crossing the 1 mm threshold in the cell above a part-full top cell used to move the
                // surface by up to (1 - f) h in one tick (measured: 0.32 m, the flicker the owner saw)
                float sum = 0.0f;
                while (y < g.ny() && g.f(x, y, z) >= fMin && g.occ(x, y, z) != Occ::Solid) { top = y; sum += std::min(g.f(x, y, z), 1.0f); ++y; }
                float pockets = 0.0f;   // 19.7: air trapped under water is drawn as water
                for (int k = start; k < top; ++k) pockets += (1.0f - std::min(g.f(x, k, z), 1.0f)) * surfacePocketWeight(std::min(g.f(x, k + 1, z), 1.0f));
                const float runTop = static_cast<float>(sp.origin.y + start) * h + (sum + pockets) * h;
                if (runs < kSurfaceMaxRuns) {
                    c.bottom[runs] = static_cast<float>(sp.origin.y + start) * h;
                    c.top[runs] = runTop;
                    ++runs;
                } else {
                    // more than kMaxRuns runs: the extra run's water stacks on the top run (the picture; mass is the solver's)
                    c.top[kSurfaceMaxRuns - 1] += sum * h;
                }
                topCell = top;
            }
            c.runs = static_cast<float>(runs);
            if (topCell >= 0) {   // E2: the surface velocity (mirrors wc_surface.comp)
                WaterGrid& gm = const_cast<WaterGrid&>(g);
                c.u = 0.5f * (gm.u(x, topCell, z) + gm.u(x + 1, topCell, z));
                c.w = 0.5f * (gm.w(x, topCell, z) + gm.w(x, topCell, z + 1));
            }
        }
}

namespace {

inline int runCount(const SurfaceColumn& c) { return static_cast<int>(c.runs + 0.5f); }

// the run of `c` that overlaps [lo, hi) in Y, or -1
inline int overlappingRun(const SurfaceColumn& c, float lo, float hi) {
    const int n = runCount(c);
    for (int r = 0; r < n; ++r) if (c.top[r] > lo && c.bottom[r] < hi) return r;
    return -1;
}

}  // namespace

void buildWaterSurfaceMesh(const WaterSurfaceField& f, WaterSurfaceMesh& out) {
    const float h = f.h;
    const float ox = static_cast<float>(f.origin.x) * h, oz = static_cast<float>(f.origin.z) * h;
    auto col = [&](int x, int z) -> const SurfaceColumn* {
        if (x < 0 || z < 0 || x >= f.nx || z >= f.nz) return nullptr;
        return &f.at(x, z);
    };
    // corner height: mean of the tops of the (up to four) sub-columns around corner (cx, cz) that hold
    // a run overlapping this run's Y range; the column itself always counts
    auto cornerY = [&](int x, int z, int cx, int cz, float lo, float hi, float ownTop) {
        float sum = 0.0f; int n = 0;
        for (int dz = -1; dz <= 0; ++dz) for (int dx = -1; dx <= 0; ++dx) {
            const int nx_ = x + cx + dx, nz_ = z + cz + dz;
            const SurfaceColumn* c = col(nx_, nz_);
            if (!c) continue;
            if (nx_ == x && nz_ == z) { sum += ownTop; ++n; continue; }
            const int r = overlappingRun(*c, lo, hi);
            if (r >= 0) { sum += c->top[r]; ++n; }
        }
        return n ? sum / static_cast<float>(n) : ownTop;
    };
    auto quad = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, float depth, float side, const glm::vec3& outward, float foam, const glm::vec2& flow) {
        glm::vec3 nrm = glm::cross(c - a, b - d);
        if (glm::dot(nrm, outward) < 0.0f) nrm = -nrm;   // the geometric normal, oriented out of the water
        const float len = glm::length(nrm);
        nrm = len > 1e-12f ? nrm / len : outward;
        const uint32_t base = static_cast<uint32_t>(out.vertices.size());
        for (const glm::vec3& p : {a, b, c, d}) out.vertices.push_back({p, depth, nrm, side, foam, flow, 0.0f});
        out.indices.insert(out.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    for (int z = 0; z < f.nz; ++z)
        for (int x = 0; x < f.nx; ++x) {
            const SurfaceColumn& c = f.at(x, z);
            const int n = runCount(c);
            const float x0 = ox + x * h, x1 = x0 + h, z0 = oz + z * h, z1 = z0 + h;
            for (int r = 0; r < n; ++r) {
                const float lo = c.bottom[r], hi = c.top[r] + h;   // a neighbour's run within one cell above still shares the surface
                const float top = c.top[r];
                const float depth = top - c.bottom[r];
                // G2: foam and flow belong to the TOP run (the free surface); deeper runs are still
                const float foam = (r == n - 1) ? c.foam : 0.0f;
                const glm::vec2 flow = (r == n - 1) ? glm::vec2(c.u, c.w) : glm::vec2(0.0f);
                // corners: (-x,-z) (+x,-z) (+x,+z) (-x,+z)
                const float y00 = cornerY(x, z, 0, 0, lo, hi, top), y10 = cornerY(x, z, 1, 0, lo, hi, top);
                const float y11 = cornerY(x, z, 1, 1, lo, hi, top), y01 = cornerY(x, z, 0, 1, lo, hi, top);
                quad({x0, y00, z0}, {x0, y01, z1}, {x1, y11, z1}, {x1, y10, z0}, depth, 0.0f, glm::vec3(0.0f, 1.0f, 0.0f), foam, flow);
                ++out.topQuads;
                // lateral faces: -x, +x, -z, +z
                struct Side { int dx, dz; glm::vec3 a, b; float ya, yb; };
                const Side sides[4] = {
                    {-1, 0, {x0, 0, z0}, {x0, 0, z1}, y00, y01},
                    {+1, 0, {x1, 0, z1}, {x1, 0, z0}, y11, y10},
                    {0, -1, {x1, 0, z0}, {x0, 0, z0}, y10, y00},
                    {0, +1, {x0, 0, z1}, {x1, 0, z1}, y01, y11},
                };
                for (const Side& s : sides) {
                    const SurfaceColumn* nb = col(x + s.dx, z + s.dz);
                    if (!nb) continue;                                  // the volume's edge: the span grid continues the water
                    if (overlappingRun(*nb, lo, hi) >= 0) continue;    // shared surface, no edge
                    if (nb->solidTopY >= top - 1e-4f) continue;         // a wall: the voxel is the edge
                    const float floorY = std::max(c.bottom[r], nb->solidTopY);
                    if (std::max(s.ya, s.yb) - floorY < 1e-4f) continue;
                    glm::vec3 a = s.a, b = s.b; a.y = s.ya; b.y = s.yb;
                    glm::vec3 c2 = b, d = a; c2.y = floorY; d.y = floorY;
                    quad(a, b, c2, d, depth, 1.0f, glm::vec3(static_cast<float>(s.dx), 0.0f, static_cast<float>(s.dz)), foam, flow);
                    ++out.sideQuads;
                }
            }
        }
}

void appendFieldToMesh(const WaterSurfaceField& field, WaterSurfaceMesh& out) {
    const uint32_t first = static_cast<uint32_t>(out.indices.size());
    buildWaterSurfaceMesh(field, out);
    const uint32_t count = static_cast<uint32_t>(out.indices.size()) - first;
    if (count > 0) out.ranges.push_back({first, count, field.look});
}

}  // namespace Phyxel::Core::Water
