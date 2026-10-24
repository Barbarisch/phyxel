#include <gtest/gtest.h>

#include "core/water/WaterSurfaceMesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

// WaterCore 26.30: the surface in tiles. A tile is a COST structure - the tiles' quads together are the whole mesh's quads, vertex
// for vertex, wherever the tile edges fall (the chunked-vs-whole shape of FloraMarginTest), and a tile is rebuilt only when what it
// reads changed.

namespace Phyxel::Core::Water {
namespace {

// a 1/9 m field straddling world tile edges on both axes (x: world -30..49, edges at 0 and 36; z: world 17..66, edge at 36), with a
// wavy surface (corners average across the edges), a dry strip across the z edge (side faces on both banks), a wall, and a patch
// of columns holding two runs
WaterSurfaceField pondField() {
    WaterSurfaceField f;
    f.origin = glm::ivec3(-30, 5, 17); f.nx = 80; f.nz = 50; f.h = 1.0f / 9.0f; f.rippleLayer = 0;
    f.cols.resize(static_cast<size_t>(f.nx) * f.nz);
    for (int z = 0; z < f.nz; ++z) for (int x = 0; x < f.nx; ++x) {
        SurfaceColumn& c = f.cols[static_cast<size_t>(x) + static_cast<size_t>(f.nx) * z];
        c.solidTopY = 0.6f;
        if (z >= 18 && z < 22) continue;                       // dry: the banks get side faces
        if (x == 60) { c.solidTopY = 5.0f; continue; }        // a wall
        const float top = 1.0f + 0.05f * std::sin(0.3f * x) + 0.03f * std::cos(0.2f * z) + (x >= 40 && x < 47 ? -0.2f : 0.0f);
        if (x >= 10 && x < 14 && z >= 30 && z < 34) {          // an overhang: two runs
            c.runs = 2; c.bottom[0] = 0.6f; c.top[0] = 0.8f; c.bottom[1] = 1.2f; c.top[1] = top + 0.4f;
        } else { c.runs = 1; c.bottom[0] = 0.6f; c.top[0] = top; }
        c.foam = 0.01f * (x % 7); c.u = 0.1f * std::sin(0.1f * z); c.w = 0.05f;
    }
    return f;
}
WaterSurfaceField flatField(glm::ivec3 origin, int n, float h) {
    WaterSurfaceField f; f.origin = origin; f.nx = n; f.nz = n; f.h = h;
    f.cols.resize(static_cast<size_t>(n) * n);
    for (auto& c : f.cols) { c.runs = 1; c.bottom[0] = 0.0f; c.top[0] = 2.0f; c.solidTopY = 0.0f; }
    return f;
}
std::vector<WaterSurfaceField> scene() { return {pondField(), flatField({100, 0, 100}, 40, 1.0f / 9.0f), flatField({-5, 0, -5}, 20, 1.0f / 3.0f)}; }

// every quad of a mesh as its 4 vertices' bytes, sorted - order-free comparison
std::vector<std::string> quads(const WaterSurfaceMesh& m) {
    std::vector<std::string> q;
    for (size_t i = 0; i + 3 < m.vertices.size(); i += 4) q.emplace_back(reinterpret_cast<const char*>(&m.vertices[i]), 4 * sizeof(WaterSurfaceVertex));
    return q;
}
std::vector<std::string> tileQuads(const WaterSurfaceTiler& t) {
    std::vector<std::string> q;
    for (const auto& [k, tile] : t.tiles()) { auto a = quads(tile.mesh); q.insert(q.end(), a.begin(), a.end()); }
    std::sort(q.begin(), q.end());
    return q;
}
std::vector<std::string> wholeQuads(const std::vector<WaterSurfaceField>& fs, long* sides = nullptr) {
    WaterSurfaceMesh m;
    for (const auto& f : fs) buildWaterSurfaceMesh(f, m);
    if (sides) *sides = m.sideQuads;
    auto q = quads(m); std::sort(q.begin(), q.end());
    return q;
}
long differing(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    std::vector<std::string> d;
    std::set_symmetric_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(d));
    return static_cast<long>(d.size());
}

}  // namespace

TEST(WaterSurfaceTileTest, TilesEqualTheWholeMesh) {
    const auto fs = scene();
    WaterSurfaceTiler tiler; tiler.update(fs);
    long sides = 0;
    const auto whole = wholeQuads(fs, &sides);
    const auto tiled = tileQuads(tiler);
    ASSERT_GT(sides, 0) << "the scene must have side faces, or the edge case is not tested";
    EXPECT_EQ(tiler.stats().tiles, 6 + 4 + 4) << "pond 3 x 2 tiles, flat 1/9 field 2 x 2 (world 100..139 crosses 108), flat 1/3 field 2 x 2";
    EXPECT_EQ(tiled.size(), whole.size()) << "as many quads in the tiles as in the whole mesh";
    EXPECT_EQ(differing(tiled, whole), 0) << "every quad of the tiles is a quad of the whole mesh, vertex for vertex";
    for (const auto& [k, t] : tiler.tiles()) {
        if (t.mesh.vertices.empty()) continue;
        ASSERT_EQ(t.mesh.ranges.size(), 1u);
        EXPECT_EQ(t.mesh.ranges[0].indexCount, t.mesh.indices.size());
        for (const auto& v : t.mesh.vertices) { EXPECT_TRUE(glm::all(glm::lessThanEqual(t.lo, v.pos)) && glm::all(glm::lessThanEqual(v.pos, t.hi))); }
    }
}

// 26.30 step 3: the rebuilds are shared over a pool; each tile is built from the field alone, so any thread count gives the same
// tiles, vertex for vertex and in the same order (the control: 1 thread, the calling thread alone)
TEST(WaterSurfaceTileTest, AnyThreadCountBuildsTheSameTiles) {
    const auto fs = scene();
    WaterSurfaceTiler one; one.setThreads(1); one.update(fs);
    EXPECT_EQ(one.stats().threads, 1);
    for (int n : {2, 4, 7}) {
        WaterSurfaceTiler many; many.setThreads(n);
        for (int rep = 0; rep < 20; ++rep) {   // churn: the pool reused across updates, every tile rebuilt each time
            auto moved = fs; for (auto& f : moved) for (auto& c : f.cols) c.top[0] += 0.001f * rep;
            many.update(rep == 19 ? fs : moved);
        }
        EXPECT_EQ(many.stats().threads, n);
        ASSERT_EQ(many.tiles().size(), one.tiles().size());
        for (auto a = one.tiles().begin(), b = many.tiles().begin(); a != one.tiles().end(); ++a, ++b) {
            ASSERT_FALSE(a->first < b->first || b->first < a->first);
            ASSERT_EQ(a->second.mesh.vertices.size(), b->second.mesh.vertices.size());
            EXPECT_EQ(std::memcmp(a->second.mesh.vertices.data(), b->second.mesh.vertices.data(), a->second.mesh.vertices.size() * sizeof(a->second.mesh.vertices[0])), 0) << n << " threads";
            EXPECT_EQ(a->second.mesh.indices, b->second.mesh.indices);
        }
        EXPECT_EQ(differing(tileQuads(many), wholeQuads(fs)), 0);
    }
}

TEST(WaterSurfaceTileTest, AnUnchangedTileIsNotRebuilt) {
    auto fs = scene();
    WaterSurfaceTiler tiler; tiler.update(fs);
    EXPECT_EQ(tiler.stats().rebuilt, tiler.stats().tiles);
    tiler.update(fs);   // no source: every tile compared, none rebuilt
    EXPECT_EQ(tiler.stats().rebuilt, 0); EXPECT_EQ(tiler.stats().kept, tiler.stats().tiles); EXPECT_EQ(tiler.stats().fieldsSkipped, 0);
    for (size_t i = 0; i < fs.size(); ++i) { fs[i].source = static_cast<int>(i) + 1; fs[i].version = 7; }
    tiler.update(fs);   // new keys (the sources): built once
    EXPECT_EQ(tiler.stats().rebuilt, tiler.stats().tiles); EXPECT_EQ(tiler.stats().removed, tiler.stats().tiles) << "the source-less tiles go";
    tiler.update(fs);   // known sources at the same version: skipped without reading
    EXPECT_EQ(tiler.stats().fieldsSkipped, 3); EXPECT_EQ(tiler.stats().rebuilt, 0); EXPECT_EQ(tiler.stats().kept, tiler.stats().tiles);
    EXPECT_EQ(differing(tileQuads(tiler), wholeQuads(fs)), 0);
}

TEST(WaterSurfaceTileTest, AChangedColumnRebuildsItsTileOnly) {
    auto fs = scene();
    WaterSurfaceTiler tiler; tiler.update(fs);
    auto bump = [&](int x, int z) { fs[0].cols[static_cast<size_t>(x) + static_cast<size_t>(fs[0].nx) * z].top[0] += 0.01f; tiler.update(fs); return tiler.stats().rebuilt; };
    EXPECT_EQ(bump(45, 40), 1) << "inside a tile (world x 15, z 57): that tile only";
    EXPECT_EQ(differing(tileQuads(tiler), wholeQuads(fs)), 0);
    EXPECT_EQ(bump(29, 40), 2) << "the last column before the x edge (world x -1): its tile and the one whose ring reads it";
    EXPECT_EQ(differing(tileQuads(tiler), wholeQuads(fs)), 0);
    EXPECT_EQ(bump(29, 18), 4) << "a corner column (world -1, 35): the four tiles round the corner";
    EXPECT_EQ(differing(tileQuads(tiler), wholeQuads(fs)), 0);
    fs[0].rippleLayer = 3; tiler.update(fs);
    EXPECT_EQ(tiler.stats().rebuilt, 6) << "the ripple layer rides in every vertex: the field's tiles all rebuild";
}

// 26.30 step 3: a pond awake at rest kept every height bit-equal while its flow jittered by ~3e-7 m/s - and rebuilt every tile
// every frame. Flow (and foam) moving less than the tolerance keeps the tile; the drift is measured from the tile's snapshot, so
// it can never add up past the tolerance; any height change, however small, still rebuilds; tolerance 0 is the exact control.
TEST(WaterSurfaceTileTest, FlowJitterWithinTheToleranceKeepsTheTile) {
    auto fs = scene();
    WaterSurfaceTiler tiler; tiler.update(fs);
    const long tiles = tiler.stats().tiles;
    auto jitterFlow = [&](float du) { for (auto& f : fs) for (auto& c : f.cols) c.u += du; tiler.update(fs); return tiler.stats().rebuilt; };
    EXPECT_EQ(jitterFlow(3e-7f), 0) << "the measured jitter: no tile rebuilt";
    EXPECT_EQ(tiler.stats().keptWithin, tiles);
    // the drift adds up from the snapshot: 4e-4 a frame passes 1e-3 on the third frame, and every tile rebuilds then (not never)
    long rebuilt = 0; int firstFrame = -1;
    for (int k = 0; k < 4; ++k) { const long r = jitterFlow(4e-4f); rebuilt += r; if (r > 0 && firstFrame < 0) firstFrame = k; }
    EXPECT_EQ(firstFrame, 2) << "rebuilt once the flow had moved 1.2e-3 from the snapshot";
    EXPECT_EQ(rebuilt, tiles);
    // a height change of a micrometre rebuilds its tile
    fs[0].cols[static_cast<size_t>(45) + static_cast<size_t>(fs[0].nx) * 40].top[0] += 1e-6f; tiler.update(fs);
    EXPECT_EQ(tiler.stats().rebuilt, 1) << "heights are compared exactly";
    // the control: tolerance 0, the same jitter rebuilds everything
    tiler.setTolerances(0.0f, 0.0f);
    EXPECT_EQ(jitterFlow(3e-7f), tiles) << "control: exact comparison rebuilds every tile on the jitter";
    EXPECT_EQ(differing(tileQuads(tiler), wholeQuads(fs)), 0) << "all rebuilt: the tiles are the whole mesh again";
}

TEST(WaterSurfaceTileTest, ARemovedFieldRemovesItsTiles) {
    auto fs = scene();
    for (size_t i = 0; i < fs.size(); ++i) { fs[i].source = static_cast<int>(i) + 1; fs[i].version = 1; }
    WaterSurfaceTiler tiler; tiler.update(fs);
    fs.erase(fs.begin() + 1);
    tiler.update(fs);
    EXPECT_EQ(tiler.stats().removed, 4); EXPECT_EQ(tiler.stats().tiles, 10);
    EXPECT_EQ(differing(tileQuads(tiler), wholeQuads(fs)), 0);
}

}  // namespace Phyxel::Core::Water

// 26.30 (found in the owner's video, 2026-10-18): the tile arenas drew each tile from vertexOffset = offset / sizeof(vertex), but
// handed out offsets in 256-byte steps - a wavy tile's vertex count is arbitrary, so its offset fell mid-vertex and the tile drew
// garbage (tile-shaped holes after a stone; the whole-mesh path drew the same water whole). Every vertex-arena offset must be a
// whole number of vertices, through allocations and frees of any size. Control: the old 256-byte arena must fail it.
#include "graphics/WaterSurfaceRenderPipeline.h"
namespace Phyxel::Core::Water {
TEST(WaterSurfaceTileTest, EveryVertexArenaOffsetIsAWholeVertex) {
    using Arena = Phyxel::Graphics::WaterSurfaceRenderPipeline::Arena;
    const VkDeviceSize vsz = sizeof(WaterSurfaceVertex);
    auto run = [&](VkDeviceSize align) {
        Arena a; a.align = align; a.size = 64ull << 20; a.freeList = {{0, a.size / align * align}};
        uint32_t h = 7u; long bad = 0, allocs = 0;
        std::vector<std::pair<VkDeviceSize, VkDeviceSize>> live;
        for (int k = 0; k < 4000; ++k) {
            h = h * 1664525u + 1013904223u;
            if (!live.empty() && (h >> 28) < 6) {   // free one (as tiles rebuild)
                const size_t i = (h >> 8) % live.size();
                a.release(live[i].first, live[i].second); live.erase(live.begin() + static_cast<long>(i));
                continue;
            }
            const VkDeviceSize bytes = vsz * (1 + (h >> 12) % 9000);   // any vertex count, as a wavy tile's
            VkDeviceSize off = 0;
            if (!a.alloc(bytes, off)) continue;
            ++allocs; live.push_back({off, bytes});
            if (off % vsz != 0) ++bad;
        }
        return std::make_pair(bad, allocs);
    };
    const auto fixed = run(Phyxel::Graphics::WaterSurfaceRenderPipeline::kVertexArenaAlign);
    const auto old = run(256);
    std::printf("  offsets mid-vertex: %ld of %ld (aligned to %llu), control (256): %ld of %ld\n", fixed.first, fixed.second,
                static_cast<unsigned long long>(Phyxel::Graphics::WaterSurfaceRenderPipeline::kVertexArenaAlign), old.first, old.second);
    EXPECT_EQ(fixed.first, 0) << "every tile's vertices start on a whole vertex";
    EXPECT_GT(old.first, 0) << "control: the 256-byte arena put tiles mid-vertex (the test sees the defect)";
}
// 27.27: a full tile arena grows instead of skipping the tile (the dry squares of the owner's lake clip). Filled with tile-sized
// blocks (churned as tiles rebuild) until one does not fit: grown, every block in use keeps its offset (the device copy keeps the
// bytes at the same offsets), the one that did not fit now fits, the free list is one block past the old end (a free tail merged),
// and nothing overlaps. Control: without the grow, the block does not fit (the test reaches the full arena).
TEST(WaterSurfaceTileTest, AFullArenaGrowsAndKeepsEveryOffset) {
    using Arena = Phyxel::Graphics::WaterSurfaceRenderPipeline::Arena;
    const VkDeviceSize vsz = sizeof(WaterSurfaceVertex);
    Arena a; a.align = Phyxel::Graphics::WaterSurfaceRenderPipeline::kVertexArenaAlign; a.size = 8ull << 20; a.freeList = {{0, a.size / a.align * a.align}};
    std::vector<std::pair<VkDeviceSize, VkDeviceSize>> live;
    uint32_t h = 11u;
    VkDeviceSize failed = 0, off = 0;
    for (int k = 0; k < 100000 && !failed; ++k) {
        h = h * 1664525u + 1013904223u;
        if (!live.empty() && (h >> 28) < 5) { const size_t i = (h >> 8) % live.size(); a.release(live[i].first, live[i].second); live.erase(live.begin() + static_cast<long>(i)); continue; }
        const VkDeviceSize bytes = vsz * (4000 + (h >> 12) % 2000);   // a tile's vertices
        if (a.alloc(bytes, off)) live.push_back({off, bytes}); else failed = bytes;
    }
    ASSERT_GT(failed, 0u) << "the arena never filled";
    VkDeviceSize probe = 0;
    EXPECT_FALSE(a.alloc(failed, probe)) << "control: full, the block does not fit";
    const auto before = live;
    const VkDeviceSize oldSize = a.size;
    a.grow(oldSize * 2);
    EXPECT_EQ(a.size, oldSize * 2);
    EXPECT_TRUE(a.alloc(failed, off)) << "grown, the block that did not fit fits";
    live.push_back({off, failed});
    EXPECT_GT(off + failed, oldSize / a.align * a.align) << "it reaches past the old end (no gap below fitted it)";
    const auto kept = decltype(live)(live.begin(), live.end() - 1);
    EXPECT_EQ(kept, before) << "every block in use kept its offset";
    std::sort(live.begin(), live.end());
    for (size_t i = 1; i < live.size(); ++i) EXPECT_LE(live[i - 1].first + (live[i - 1].second + a.align - 1) / a.align * a.align, live[i].first) << "no overlap";
    EXPECT_LE(live.back().first + live.back().second, a.size);
    for (const auto& [o, b] : a.freeList) EXPECT_LE(o + b, a.size);
    std::printf("  filled at %zu blocks (%.1f of %.1f MB in use), grown to %.1f MB; the failed block now at %.1f MB\n", before.size(),
                static_cast<double>(a.used - (failed + a.align - 1) / a.align * a.align) / (1 << 20), static_cast<double>(oldSize) / (1 << 20),
                static_cast<double>(a.size) / (1 << 20), static_cast<double>(off) / (1 << 20));
}
}  // namespace Phyxel::Core::Water
