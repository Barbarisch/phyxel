#include "core/water/WaterSurfaceMesh.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstring>
#include <cmath>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace Phyxel::Core::Water {

double recordSurfaceField(WaterGrid& scratch, const std::vector<ColumnRuns>& runs, WaterSurfaceField& out) {
    const double placed = seedGridFromRuns(scratch, runs);   // the water a wake would make (air cells, level-filled, at rest)
    extractSurfaceField(scratch, out);                       // drawn exactly as the awake pond draws it
    return placed;
}

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
                while (y < g.ny() && g.f(x, y, z) >= fMin && g.occ(x, y, z) != Occ::Solid) { top = y; sum += g.levelFill(x, y, z); ++y; }   // 20.5: water + the body it surrounds
                float pockets = 0.0f;   // 19.7: air trapped under water is drawn as water
                for (int k = start; k < top; ++k) pockets += (1.0f - g.levelFill(x, k, z)) * surfacePocketWeight(g.levelFill(x, k + 1, z));
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
            if (topCell >= 0) {   // 20: a body at the surface (mirrors wc_surface.comp)
                c.bodyAtSurface = g.s(x, topCell, z);
                if (topCell + 1 < g.ny()) c.bodyAtSurface = std::max(c.bodyAtSurface, g.s(x, topCell + 1, z));
            }
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

void buildWaterSurfaceMesh(const WaterSurfaceField& f, WaterSurfaceMesh& out) { buildWaterSurfaceMeshRect(f, 0, 0, f.nx, f.nz, out); }

void buildWaterSurfaceMeshRect(const WaterSurfaceField& f, int rx0, int rz0, int rx1, int rz1, WaterSurfaceMesh& out) {
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
        // 22 (22.8 F1): the spare float carries the ripple layer as index + 1 - 0 keeps meaning "none"
        for (const glm::vec3& p : {a, b, c, d}) out.vertices.push_back({p, depth, nrm, side, foam, flow, static_cast<float>(f.rippleLayer + 1)});
        out.indices.insert(out.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    for (int z = std::max(rz0, 0); z < std::min(rz1, f.nz); ++z)
        for (int x = std::max(rx0, 0); x < std::min(rx1, f.nx); ++x) {
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

void appendDropletsToMesh(const std::vector<glm::vec4>& droplets, WaterSurfaceMesh& out) {
    if (droplets.empty()) return;
    const uint32_t first = static_cast<uint32_t>(out.indices.size());
    static const glm::vec3 kN[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const glm::vec4& d : droplets) {
        const glm::vec3 c(d.x, d.y, d.z);
        const float e = 0.5f * d.w;
        for (const glm::vec3& n : kN) {
            // two axes spanning the face, chosen so (a, b, c, d) winds the same way for every face
            const glm::vec3 t = std::abs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            const glm::vec3 bt = glm::cross(n, t);
            const glm::vec3 fc = c + n * e;
            const uint32_t base = static_cast<uint32_t>(out.vertices.size());
            for (const glm::vec2 k : {glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1)})
                out.vertices.push_back({fc + (t * k.x + bt * k.y) * e, d.w, n, 2.0f, 1.0f, glm::vec2(0.0f), 0.0f});
            out.indices.insert(out.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        }
    }
    const uint32_t count = static_cast<uint32_t>(out.indices.size()) - first;
    if (count > 0) out.ranges.push_back({first, count, WaterLookPacked{}});
}

void appendFieldToMesh(const WaterSurfaceField& field, WaterSurfaceMesh& out) {
    const uint32_t first = static_cast<uint32_t>(out.indices.size());
    buildWaterSurfaceMesh(field, out);
    const uint32_t count = static_cast<uint32_t>(out.indices.size()) - first;
    if (count > 0) out.ranges.push_back({first, count, field.look});
}

}  // namespace Phyxel::Core::Water

namespace Phyxel::Core::Water {
namespace {
int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
bool sameLook(const WaterLookPacked& a, const WaterLookPacked& b) { return std::memcmp(&a, &b, sizeof a) == 0; }
}  // namespace

// 26.30 step 3: a small persistent pool - run(n, fn) calls fn(0..n-1) over the workers and the caller, and returns when all ran
struct WaterSurfaceTiler::Pool {
    std::vector<std::thread> workers;
    std::mutex m; std::condition_variable go, done;
    std::function<void(size_t)> fn; size_t count = 0; std::atomic<size_t> next{0};
    uint64_t generation = 0; int busy = 0; bool quit = false;
    explicit Pool(int n) { for (int i = 0; i < n; ++i) workers.emplace_back([this] { loop(); }); }
    ~Pool() { { std::lock_guard<std::mutex> l(m); quit = true; } go.notify_all(); for (auto& w : workers) w.join(); }
    void drain() { for (size_t i; (i = next.fetch_add(1)) < count;) fn(i); }
    void loop() {
        uint64_t seen = 0;
        for (;;) {
            std::unique_lock<std::mutex> l(m);
            go.wait(l, [&] { return quit || generation != seen; });
            if (quit) return;
            seen = generation; ++busy; l.unlock();
            drain();
            l.lock(); if (--busy == 0) done.notify_all();
        }
    }
    void run(size_t n, std::function<void(size_t)> f) {
        { std::lock_guard<std::mutex> l(m); fn = std::move(f); count = n; next = 0; ++generation; }
        go.notify_all();
        drain();
        std::unique_lock<std::mutex> l(m);
        done.wait(l, [&] { return busy == 0 && next.load() >= count; });
    }
};

WaterSurfaceTiler::WaterSurfaceTiler() { setThreads(0); }
WaterSurfaceTiler::~WaterSurfaceTiler() = default;

void WaterSurfaceTiler::setThreads(int n) {
    if (n <= 0) n = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 2, 1, 8);
    n = std::clamp(n, 1, 32);
    if (n == m_threads && (n == 1 || m_pool)) return;
    m_pool.reset();
    m_threads = n;
    if (n > 1) m_pool = std::make_unique<Pool>(n - 1);
}

void WaterSurfaceTiler::update(const std::vector<const WaterSurfaceField*>& fields) {
    const auto t0 = std::chrono::steady_clock::now();
    m_stats = Stats{};
    struct Job { SurfaceTile* t; const WaterSurfaceField* f; int x0, z0, x1, z1; };
    std::vector<Job> jobs;
    for (auto& [k, t] : m_tiles) t.seen = false;
    for (size_t fi = 0; fi < fields.size(); ++fi) {
        if (!fields[fi]) continue;
        const WaterSurfaceField& f = *fields[fi];
        if (f.nx <= 0 || f.nz <= 0) continue;
        const int source = f.source != 0 ? f.source : -(1000 + static_cast<int>(fi));
        // a known source at an unchanged version: its tiles stand as they are (nothing read)
        FieldMark& mark = m_fieldVersion[source];
        const bool sameField = mark.origin == f.origin && mark.nx == f.nx && mark.nz == f.nz && mark.h == f.h && mark.rippleLayer == f.rippleLayer && sameLook(mark.look, f.look);
        if (f.source != 0 && f.version != 0 && sameField && mark.version == f.version) {
            for (auto it = m_tiles.lower_bound(SurfaceTileKey{source, INT_MIN, INT_MIN}); it != m_tiles.end() && it->first.source == source; ++it) { it->second.seen = true; ++m_stats.kept; }
            ++m_stats.fieldsSkipped;
            continue;
        }
        mark = FieldMark{f.version, f.origin, f.nx, f.nz, f.h, f.rippleLayer, f.look};
        const int tx0 = floorDiv(f.origin.x, kSurfaceTileCols), tx1 = floorDiv(f.origin.x + f.nx - 1, kSurfaceTileCols);
        const int tz0 = floorDiv(f.origin.z, kSurfaceTileCols), tz1 = floorDiv(f.origin.z + f.nz - 1, kSurfaceTileCols);
        for (int tz = tz0; tz <= tz1; ++tz) for (int tx = tx0; tx <= tx1; ++tx) {
            // the tile's columns, field-local, and the snapshot rect (a one-column ring, clipped to the field)
            const int x0 = std::max(0, tx * kSurfaceTileCols - f.origin.x), x1 = std::min(f.nx, (tx + 1) * kSurfaceTileCols - f.origin.x);
            const int z0 = std::max(0, tz * kSurfaceTileCols - f.origin.z), z1 = std::min(f.nz, (tz + 1) * kSurfaceTileCols - f.origin.z);
            const int sx0 = std::max(0, x0 - 1), sx1 = std::min(f.nx, x1 + 1), sz0 = std::max(0, z0 - 1), sz1 = std::min(f.nz, z1 + 1);
            const SurfaceTileKey key{source, tx, tz};
            SurfaceTile& t = m_tiles[key];
            t.seen = true;
            const size_t w = static_cast<size_t>(sx1 - sx0);
            const bool header = t.generation != 0 && t.origin == f.origin && t.nx == f.nx && t.nz == f.nz && t.h == f.h && t.rippleLayer == f.rippleLayer && sameLook(t.look, f.look)
                                && t.rx0 == sx0 && t.rz0 == sz0 && t.rx1 == sx1 && t.rz1 == sz1 && t.input.size() == w * static_cast<size_t>(sz1 - sz0);
            bool same = header;
            for (int z = sz0; z < sz1 && same; ++z)
                same = std::memcmp(&t.input[static_cast<size_t>(z - sz0) * w], &f.at(sx0, z), w * sizeof(SurfaceColumn)) == 0;
            if (same) { ++m_stats.kept; continue; }
            if (header) {
                // 26.30 step 3: how far the columns moved since the snapshot. Measured on a 32 x 28 m pond awake at rest: every
                // height EXACTLY equal, only the flow jittering by ~3e-7 m/s - and every tile rebuilt every frame (9 ms). A tile
                // whose run counts, heights and body fractions are bit-equal and whose flow and foam moved less than the
                // tolerances below is KEPT (the snapshot stays, so the drawn flow is never more than the tolerance off).
                float dH = 0.0f, dF = 0.0f, dFo = 0.0f, dB = 0.0f; bool runs = false;
                bool keep = true;   // without the measurement the scan stops at the first column that forbids keeping the tile
                for (int z = sz0; z < sz1 && (keep || m_measure); ++z) for (int x = sx0; x < sx1 && (keep || m_measure); ++x) {
                    const SurfaceColumn& a = t.input[static_cast<size_t>(z - sz0) * w + (x - sx0)];
                    const SurfaceColumn& b = f.at(x, z);
                    if (a.runs != b.runs) runs = true;
                    for (int r = 0; r < kSurfaceMaxRuns; ++r) dH = std::max(dH, std::max(std::abs(a.top[r] - b.top[r]), std::abs(a.bottom[r] - b.bottom[r])));
                    if (a.solidTopY != b.solidTopY) dH = std::max(dH, std::abs(a.solidTopY - b.solidTopY));
                    dF = std::max(dF, std::max(std::abs(a.u - b.u), std::abs(a.w - b.w)));
                    dFo = std::max(dFo, std::abs(a.foam - b.foam)); dB = std::max(dB, std::abs(a.bodyAtSurface - b.bodyAtSurface));
                    keep = !runs && dH == 0.0f && dB == 0.0f && dF <= m_flowTolerance && dFo <= m_foamTolerance;
                }
                if (m_measure) {
                    m_stats.maxDHeight = std::max(m_stats.maxDHeight, dH); m_stats.maxDFlow = std::max(m_stats.maxDFlow, dF);
                    m_stats.maxDFoam = std::max(m_stats.maxDFoam, dFo); m_stats.maxDBody = std::max(m_stats.maxDBody, dB);
                }
                if (keep) { ++m_stats.kept; ++m_stats.keptWithin; continue; }
                if (m_measure) {
                    m_stats.runsChanged += runs;
                    ++m_stats.heightBins[dH < 1e-5f ? 0 : dH < 1e-4f ? 1 : dH < 1e-3f ? 2 : dH < 1e-2f ? 3 : 4];
                }
            }
            t.origin = f.origin; t.nx = f.nx; t.nz = f.nz; t.h = f.h; t.rippleLayer = f.rippleLayer; t.look = f.look;
            t.rx0 = sx0; t.rz0 = sz0; t.rx1 = sx1; t.rz1 = sz1;
            ++t.generation;
            ++m_stats.rebuilt;
            jobs.push_back({&t, &f, x0, z0, x1, z1});   // built below, over the pool (each from the field alone)
        }
    }
    const auto tb = std::chrono::steady_clock::now();
    auto build = [&](size_t i) {
        const Job& j = jobs[i];
        SurfaceTile& t = *j.t; const WaterSurfaceField& f = *j.f;
        const size_t w = static_cast<size_t>(t.rx1 - t.rx0);
        t.input.resize(w * static_cast<size_t>(t.rz1 - t.rz0));
        for (int z = t.rz0; z < t.rz1; ++z) std::memcpy(&t.input[static_cast<size_t>(z - t.rz0) * w], &f.at(t.rx0, z), w * sizeof(SurfaceColumn));
        t.mesh.clear();
        buildWaterSurfaceMeshRect(f, j.x0, j.z0, j.x1, j.z1, t.mesh);   // the whole mesh's quads for these columns (the corners read across the edge)
        if (!t.mesh.indices.empty()) t.mesh.ranges.push_back({0, static_cast<uint32_t>(t.mesh.indices.size()), f.look});
        t.lo = glm::vec3(1e30f); t.hi = glm::vec3(-1e30f);
        for (const auto& v : t.mesh.vertices) { t.lo = glm::min(t.lo, v.pos); t.hi = glm::max(t.hi, v.pos); }
    };
    if (m_pool && jobs.size() > 1) m_pool->run(jobs.size(), build);
    else for (size_t i = 0; i < jobs.size(); ++i) build(i);
    m_stats.threads = (m_pool && jobs.size() > 1) ? m_threads : 1;
    m_stats.buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tb).count();
    for (auto it = m_tiles.begin(); it != m_tiles.end();) {
        if (!it->second.seen) { it = m_tiles.erase(it); ++m_stats.removed; } else ++it;
    }
    for (auto it = m_fieldVersion.begin(); it != m_fieldVersion.end();) {   // forget sources with no tiles left
        bool any = false;
        auto t = m_tiles.lower_bound(SurfaceTileKey{it->first, INT_MIN, INT_MIN});
        any = t != m_tiles.end() && t->first.source == it->first;
        it = any ? std::next(it) : m_fieldVersion.erase(it);
    }
    m_stats.tiles = static_cast<long>(m_tiles.size());
    m_stats.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

}  // namespace Phyxel::Core::Water
