// VoxFoundFaceBuilderGpuTest — docs/VoxelFoundationPlan.md F2.1, §4.6 items 1-2.
//
// The GPU face builder (FaceBuilderGpu, shaders/voxfound_faces.comp) is accepted only on EXACT equality
// with the CPU reference meshRuns: the same packed 8-byte faces, as a set. Needs a Vulkan device: the
// fixture GTEST_SKIPs without one (never a pass).
//
// Cases: hand rigs; random content at awkward sizes (1 voxel, sides not a multiple of 4 or 8, a full
// 256 side) with both Outside modes; engine-generated Perlin terrain (two seeds); a 3-D checkerboard
// (no merging possible: 786,432 faces) that overflows the starting face buffer so the regrow path runs;
// refusals for a 0 side and a 257 side.
#include <gtest/gtest.h>
#include "IntegrationTestFixture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <random>
#include <array>
#include <string>
#include <tuple>
#include <vector>

#include "core/WorldGenerator.h"
#include "core/voxfound/ClusterCullGpu.h"
#include "core/voxfound/FaceBuilderGpu.h"
#include "core/voxfound/Mesher.h"
#include "core/voxfound/StorageLab.h"
#include "utils/Frustum.h"
#include <glm/gtc/matrix_transform.hpp>

using namespace Phyxel;
using namespace Phyxel::Core;
using namespace Phyxel::Core::VoxFound;
using Phyxel::Testing::VulkanTestFixture;

namespace {

VoxelGrid box(glm::ivec3 d) {
    VoxelGrid g;
    g.resize(d);
    return g;
}

std::vector<uint64_t> keys(const std::vector<PackedFace>& f) {
    std::vector<uint64_t> k;
    k.reserve(f.size());
    for (const PackedFace& p : f) k.push_back(p.key());
    std::sort(k.begin(), k.end());
    return k;
}

std::vector<PackedFace> cpuFaces(const VoxelGrid& g, Edges o, const SprigParams* sp = nullptr) {
    std::vector<PackedFace> f;
    for (const Quad& q : meshRuns(g, o, sp ? &sp->cards : nullptr)) f.push_back(packFace(q));
    if (sp)
        for (const PackedFace& s : foliageSprigs(g, o, *sp)) f.push_back(s);
    return f;
}

std::string firstDiff(const std::vector<uint64_t>& cpu, const std::vector<uint64_t>& gpu) {
    std::vector<uint64_t> onlyCpu, onlyGpu;
    std::set_difference(cpu.begin(), cpu.end(), gpu.begin(), gpu.end(), std::back_inserter(onlyCpu));
    std::set_difference(gpu.begin(), gpu.end(), cpu.begin(), cpu.end(), std::back_inserter(onlyGpu));
    auto show = [](uint64_t k) {
        const Quad q = unpackFace({uint32_t(k >> 32), uint32_t(k)});
        char b[128];
        std::snprintf(b, sizeof(b), "(%d,%d,%d) axis %d dir %d %dx%d mat %d", q.o.x, q.o.y, q.o.z, q.axis, q.dir,
                      q.du, q.dv, q.material);
        return std::string(b);
    };
    std::string s = std::to_string(onlyCpu.size()) + " only on CPU, " + std::to_string(onlyGpu.size()) + " only on GPU";
    if (!onlyCpu.empty()) s += "; first CPU-only " + show(onlyCpu[0]);
    if (!onlyGpu.empty()) s += "; first GPU-only " + show(onlyGpu[0]);
    return s;
}

class VoxFoundFaceBuilderGpu : public VulkanTestFixture {
protected:
    void SetUp() override {
        VulkanTestFixture::SetUp();
        if (!isVulkanAvailable() || device == VK_NULL_HANDLE) GTEST_SKIP() << "no Vulkan device";
        std::string err;
        if (!gpu.init(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err))
            GTEST_SKIP() << "FaceBuilderGpu init: " << err;
    }
    void TearDown() override {
        gpu.shutdown();
        VulkanTestFixture::TearDown();
    }
    // Builds on the GPU and compares with the CPU reference; returns the face count.
    size_t same(const VoxelGrid& g, Edges o, const std::string& name, const SprigParams* sp = nullptr) {
        std::vector<PackedFace> f;
        std::string err;
        EXPECT_TRUE(gpu.build(g, o, f, &err, sp)) << name << ": " << err;
        const auto c = keys(cpuFaces(g, o, sp)), k = keys(f);
        EXPECT_EQ(c.size(), k.size()) << name;
        EXPECT_TRUE(c == k) << name << ": " << firstDiff(c, k);
        return k.size();
    }
    FaceBuilderGpu gpu;
};

}  // namespace

TEST_F(VoxFoundFaceBuilderGpu, HandRigsEqualTheReference) {
    VoxelGrid one = box({3, 3, 3});
    one.set({1, 1, 1}, 1);
    EXPECT_EQ(same(one, Outside::Air, "single voxel"), 6u);
    VoxelGrid blk = box({8, 8, 8});
    for (size_t i = 0; i < blk.size(); ++i) blk.mat[i] = 1;
    EXPECT_EQ(same(blk, Outside::Air, "8^3 block"), 6u);
    EXPECT_EQ(same(blk, Outside::Skip, "8^3 block, skip"), 0u);
    VoxelGrid wall = box({256, 256, 1});
    for (size_t i = 0; i < wall.size(); ++i) wall.mat[i] = 1;
    EXPECT_EQ(same(wall, Outside::Air, "256x256x1 wall"), 6u);
}

TEST_F(VoxFoundFaceBuilderGpu, RandomContentAtAwkwardSizesEqualsTheReference) {
    const glm::ivec3 sizes[] = {{1, 1, 1}, {24, 20, 28}, {13, 7, 5}, {256, 7, 3}, {5, 256, 9}, {3, 9, 256}, {33, 65, 17}};
    std::mt19937 rng(23);
    for (glm::ivec3 d : sizes) {
        VoxelGrid g = box(d);
        for (size_t i = 0; i < g.size(); ++i) {
            const unsigned r = rng() % 100;
            g.mat[i] = r < 40 ? 1 : r < 50 ? 2 : r < 52 ? 255 : 0;
        }
        for (Outside o : {Outside::Air, Outside::Skip})
            same(g, o, std::to_string(d.x) + "x" + std::to_string(d.y) + "x" + std::to_string(d.z) +
                           (o == Outside::Air ? " air" : " skip"));
    }
}

TEST_F(VoxFoundFaceBuilderGpu, GeneratedTerrainEqualsTheReference) {
    struct Spot { uint32_t seed; int x, z; };
    for (Spot s : {Spot{7u, 0, 0}, Spot{12345u, -1024, 768}}) {
        WorldGenerator gen(WorldGenerator::GenerationType::Perlin, s.seed);
        float lo = std::numeric_limits<float>::max(), hi = -lo;
        for (int x = s.x - 1; x <= s.x + 32; ++x)
            for (int z = s.z - 1; z <= s.z + 32; ++z) {
                const float h = gen.sampleSurface(x, z).bedHeight;
                lo = std::min(lo, h);
                hi = std::max(hi, h);
            }
        const int y0 = int(std::floor(lo)) - 8, y1 = int(std::ceil(hi)) + 8;
        const VoxelArea a = buildTerrainArea(gen, {s.x, y0, s.z}, {32, y1 - y0, 32});
        ASSERT_LE(a.grid.dim.y, kMaxChunkVoxels) << "terrain window taller than one chunk";
        const size_t n = same(a.grid, Outside::Skip, "perlin s" + std::to_string(s.seed));
        std::printf("[facebuilder] perlin s%u (%d,%d) %dx%dx%d voxels: %zu faces, GPU %.3f ms\n", s.seed, s.x, s.z,
                    a.grid.dim.x, a.grid.dim.y, a.grid.dim.z, n, gpu.lastGpuMs());
    }
}

TEST_F(VoxFoundFaceBuilderGpu, OverflowRegrowsAndStaysExact) {
    gpu.setInitialCapacity(4096);
    VoxelGrid g = box({64, 64, 64});
    for (int x = 0; x < 64; ++x)
        for (int y = 0; y < 64; ++y)
            for (int z = 0; z < 64; ++z)
                if ((x + y + z) % 2 == 0) g.set({x, y, z}, 1);
    EXPECT_EQ(same(g, Outside::Air, "checkerboard 64^3"), 64u * 64u * 64u / 2u * 6u);
    EXPECT_GE(gpu.capacity(), 64u * 64u * 64u / 2u * 6u);
}

TEST_F(VoxFoundFaceBuilderGpu, RefusesGridsThatAreNotOneChunk) {
    std::vector<PackedFace> f;
    std::string err;
    EXPECT_FALSE(gpu.build(box({257, 1, 1}), Outside::Air, f, &err));
    EXPECT_NE(err.find("256"), std::string::npos) << err;
    err.clear();
    EXPECT_FALSE(gpu.build(box({0, 4, 4}), Outside::Air, f, &err));
    EXPECT_FALSE(err.empty());
}

// ---- F2.3 leaf cards: card materials emit no faces; exposed card cells become sprigs (axis-3 records) ----

TEST_F(VoxFoundFaceBuilderGpu, CardsAndSprigsEqualTheReference) {
    // Random content, cards = {2, 255} (255 exercises the high CardSet word), a negative world origin, two
    // thresholds (keep all; the metric 1/8 m threshold), both Outside modes, awkward sizes.
    const glm::ivec3 sizes[] = {{1, 1, 1}, {24, 20, 28}, {13, 7, 5}, {256, 7, 3}, {33, 65, 17}};
    std::mt19937 rng(41);
    for (uint32_t thr : {1u << 24, sprigThreshold24(kSprigsPerM2, kVoxelsPerM)}) {
        SprigParams sp;
        sp.cards.set(2);
        sp.cards.set(255);
        sp.worldOriginVoxel = {-1000, 37, 123456};
        sp.threshold24 = thr;
        for (glm::ivec3 d : sizes) {
            VoxelGrid g = box(d);
            for (size_t i = 0; i < g.size(); ++i) {
                const unsigned r = rng() % 100;
                g.mat[i] = r < 30 ? 1 : r < 50 ? 2 : r < 55 ? 255 : 0;
            }
            for (Outside o : {Outside::Air, Outside::Skip}) {
                const std::string name = std::to_string(d.x) + "x" + std::to_string(d.y) + "x" + std::to_string(d.z) +
                                         (o == Outside::Air ? " air" : " skip") + " thr " + std::to_string(thr);
                same(g, o, name, &sp);
                size_t sprigs = 0;
                for (const PackedFace& f : foliageSprigs(g, o, sp)) sprigs += isSprig(f);
                if (d == glm::ivec3(24, 20, 28) && thr == (1u << 24)) EXPECT_GT(sprigs, 0u) << name;
            }
        }
    }
}

TEST_F(VoxFoundFaceBuilderGpu, ForestWithFloraEqualsTheReference) {
    // The F2.2 forest spot: engine terrain + engine flora plan stamped from the 1/8 m tree library.
    namespace fs = std::filesystem;
    fs::path root = fs::current_path();
    for (int i = 0; i < 6 && !fs::exists(root / "resources" / "templates_g8"); ++i) root = root.parent_path();
    ASSERT_TRUE(fs::exists(root / "resources" / "templates_g8")) << "repo root not found from " << fs::current_path();
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    gen.loadBiomes((root / "resources" / "biomes.json").string());
    const int x0 = 8160, z0 = 96;
    float lo = std::numeric_limits<float>::max();
    for (int x = x0 - 1; x <= x0 + 32; ++x)
        for (int z = z0 - 1; z <= z0 + 32; ++z) lo = std::min(lo, gen.sampleSurface(x, z).bedHeight);
    const int y0 = int(std::floor(lo)) - 2;
    VoxelArea a = buildTerrainArea(gen, {x0, y0, z0}, {32, 32, 32});
    const FloraStampStats flora = stampFlora(a, gen, (root / "resources" / "templates_g8" / "nature").string());
    ASSERT_GT(flora.stamped, 0u);
    SprigParams sp;
    for (size_t m = 0; m < a.palette.size(); ++m)
        for (const char* leaf : {"Leaf", "LeafBirch", "LeafSpruce", "LeafJungle", "LeafAutumn"})
            if (a.palette[m] == leaf) sp.cards.set(uint8_t(m));
    sp.worldOriginVoxel = a.originVoxel;
    sp.threshold24 = sprigThreshold24(kSprigsPerM2, kVoxelsPerM);
    const size_t all = same(a.grid, Outside::Skip, "forest", &sp);
    size_t sprigs = 0;
    for (const PackedFace& f : cpuFaces(a.grid, Outside::Skip, &sp)) sprigs += isSprig(f);
    EXPECT_GT(sprigs, 0u);
    std::printf("[facebuilder] forest (%d,%d) %zu trees: solid faces %zu (vs %zu with leaves as voxels), "
                "sprigs %zu, GPU %.3f ms\n", x0, z0, flora.stamped, all - sprigs, meshRuns(a.grid, Outside::Skip).size(),
                sprigs, gpu.lastGpuMs());
}

// ---- F2.6 uniform-block skip: rows jump over 8^3 blocks that cannot hold a face; output unchanged ----

namespace {
// Awkward size (no side a multiple of 8, so every far-edge block is partial) with every block kind the skip
// distinguishes: solid stone below y 30 (hides everything), air above y 46 (emits nothing), a random band between,
// a solid block with ONE air voxel (must not count as hiding everything), a block of card material (emits nothing
// but hides its neighbours), and a card block that touches stone.
VoxelGrid skipRig() {
    VoxelGrid g = box({37, 70, 19});
    std::mt19937 rng(77);
    for (int x = 0; x < 37; ++x)
        for (int y = 0; y < 70; ++y)
            for (int z = 0; z < 19; ++z) {
                uint8_t m = 0;
                if (y < 30) m = 1;
                else if (y < 46) { const unsigned r = rng() % 100; m = r < 35 ? 1 : r < 45 ? 3 : r < 55 ? 2 : 0; }
                g.set({x, y, z}, m);
            }
    g.set({11, 13, 4}, 0);                                              // one air voxel in block (1,1,0)
    // Lone voxels on block CORNERS, so a block pass that misses a block's first or last layer is caught
    // (a mutation reading 7 layers per block passed the rig without these).
    g.set({15, 15, 7}, 0);                                              // air, last layer of stone block (1,1,0)
    g.set({16, 16, 8}, 0);                                              // air, first layer of stone block (2,2,1)
    g.set({7, 55, 7}, 1);                                               // stone, last layer of air block (0,6,0)
    g.set({8, 56, 8}, 1);                                               // stone, first layer of air block (1,7,1)
    g.set({36, 10, 18}, 0);                                             // air, last in-grid layer of edge block (4,1,2)
    for (int x = 16; x < 24; ++x)
        for (int y = 48; y < 56; ++y)
            for (int z = 8; z < 16; ++z) g.set({x, y, z}, 2);           // a whole card block in the air
    for (int x = 24; x < 32; ++x)
        for (int y = 24; y < 32; ++y)
            for (int z = 0; z < 8; ++z) g.set({x, y, z}, 2);            // a card block touching stone and the band
    return g;
}
}  // namespace

TEST_F(VoxFoundFaceBuilderGpu, UniformBlockSkipSkipsAndStaysExact) {
    const VoxelGrid g = skipRig();
    SprigParams sp;
    sp.cards.set(2);
    sp.worldOriginVoxel = {-3, 5, 999};
    sp.threshold24 = 1u << 24;
    for (bool skip : {false, true}) {
        gpu.setUniformBlockSkip(skip);
        for (Outside o : {Outside::Air, Outside::Skip})
            for (const SprigParams* p : {(const SprigParams*)nullptr, (const SprigParams*)&sp}) {
                const std::string name = std::string(skip ? "skip on" : "skip off") + (o == Outside::Air ? " air" : " skip") +
                                         (p ? " cards" : "");
                same(g, o, name, p);
                if (skip) EXPECT_GT(gpu.lastSkippedSpans(), 0u) << name << ": the rig has uniform blocks";
                else EXPECT_EQ(gpu.lastSkippedSpans(), 0u) << name;
            }
    }
}

TEST_F(VoxFoundFaceBuilderGpu, UniformBlockSkipTiming) {
    // Measurement, not a gate: device time of the same build with the skip off and on (best of 5 each).
    namespace fs = std::filesystem;
    fs::path root = fs::current_path();
    for (int i = 0; i < 6 && !fs::exists(root / "resources" / "templates_g8"); ++i) root = root.parent_path();
    ASSERT_TRUE(fs::exists(root / "resources" / "templates_g8"));
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    gen.loadBiomes((root / "resources" / "biomes.json").string());
    struct Case { const char* name; VoxelArea a; SprigParams sp; bool cards; };
    std::vector<Case> cases;
    {
        float lo = std::numeric_limits<float>::max(), hi = -lo;
        for (int x = -1; x <= 32; ++x)
            for (int z = -1; z <= 32; ++z) {
                const float h = gen.sampleSurface(x, z).bedHeight;
                lo = std::min(lo, h);
                hi = std::max(hi, h);
            }
        const int y0 = int(std::floor(lo)) - 8, y1 = int(std::ceil(hi)) + 8;
        cases.push_back({"perlin s7 (0,0)", buildTerrainArea(gen, {0, y0, 0}, {32, y1 - y0, 32}), {}, false});
    }
    {
        float lo = std::numeric_limits<float>::max();
        for (int x = 8159; x <= 8192; ++x)
            for (int z = 95; z <= 128; ++z) lo = std::min(lo, gen.sampleSurface(x, z).bedHeight);
        VoxelArea a = buildTerrainArea(gen, {8160, int(std::floor(lo)) - 2, 96}, {32, 32, 32});
        stampFlora(a, gen, (root / "resources" / "templates_g8" / "nature").string());
        SprigParams sp;
        for (size_t m = 0; m < a.palette.size(); ++m)
            for (const char* leaf : {"Leaf", "LeafBirch", "LeafSpruce", "LeafJungle", "LeafAutumn"})
                if (a.palette[m] == leaf) sp.cards.set(uint8_t(m));
        sp.worldOriginVoxel = a.originVoxel;
        sp.threshold24 = sprigThreshold24(kSprigsPerM2, kVoxelsPerM);
        cases.push_back({"forest (8160,96)", std::move(a), sp, true});
    }
    for (const Case& c : cases) {
        double best[2] = {1e30, 1e30};
        size_t faces[2] = {0, 0}, spans = 0;
        for (int on = 0; on < 2; ++on) {
            gpu.setUniformBlockSkip(on != 0);
            for (int rep = 0; rep < 5; ++rep) {
                std::vector<PackedFace> f;
                std::string err;
                ASSERT_TRUE(gpu.build(c.a.grid, Outside::Skip, f, &err, c.cards ? &c.sp : nullptr)) << err;
                best[on] = std::min(best[on], gpu.lastGpuMs());
                faces[on] = f.size();
                if (on) spans = gpu.lastSkippedSpans();
            }
        }
        EXPECT_EQ(faces[0], faces[1]) << c.name;
        std::printf("[facebuilder] skip timing %s %dx%dx%d: off %.3f ms, on %.3f ms, %zu spans skipped, %zu records\n",
                    c.name, c.a.grid.dim.x, c.a.grid.dim.y, c.a.grid.dim.z, best[0], best[1], spans, faces[1]);
    }
}

// ---- F2.7 chunk borders: GPU build with Neighbours; the chunks must be invisible ----

namespace {
using UnitFace = std::tuple<int, int, int, int, int, int>;   // world x, y, z, axis, dir, material

void addUnitFaces(const std::vector<PackedFace>& faces, glm::ivec3 lo, std::vector<UnitFace>& out,
                  std::vector<std::tuple<int, int, int, int>>& sprigs) {
    for (const PackedFace& f : faces) {
        if (isSprig(f)) {
            sprigs.emplace_back(lo.x + int(f.a & 0xFF), lo.y + int((f.a >> 8) & 0xFF), lo.z + int((f.a >> 16) & 0xFF),
                                int(f.b >> 16));
            continue;
        }
        const Quad q = unpackFace(f);
        const int ua = (q.axis + 1) % 3, va = (q.axis + 2) % 3;
        for (int i = 0; i < q.du; ++i)
            for (int j = 0; j < q.dv; ++j) {
                glm::ivec3 p = q.o + lo;
                p[ua] += i;
                p[va] += j;
                out.emplace_back(p.x, p.y, p.z, int(q.axis), int(q.dir), int(q.material));
            }
    }
}
}  // namespace

TEST_F(VoxFoundFaceBuilderGpu, ChunksAreInvisible) {
    // Whole region (CPU meshCulled + foliageSprigs, no chunks) vs the union of GPU chunk builds, each with its
    // Neighbours. Also each chunk GPU == CPU (meshRuns with the same Neighbours) as a set.
    namespace fs = std::filesystem;
    fs::path root = fs::current_path();
    for (int i = 0; i < 6 && !fs::exists(root / "resources" / "templates_g8"); ++i) root = root.parent_path();
    ASSERT_TRUE(fs::exists(root / "resources" / "templates_g8"));
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    gen.loadBiomes((root / "resources" / "biomes.json").string());
    float lo = std::numeric_limits<float>::max();
    for (int x = 8159; x <= 8192; ++x)
        for (int z = 95; z <= 128; ++z) lo = std::min(lo, gen.sampleSurface(x, z).bedHeight);
    VoxelArea forest = buildTerrainArea(gen, {8160, int(std::floor(lo)) - 2, 96}, {32, 32, 32});
    stampFlora(forest, gen, (root / "resources" / "templates_g8" / "nature").string());
    SprigParams fsp;
    for (size_t m = 0; m < forest.palette.size(); ++m)
        for (const char* leaf : {"Leaf", "LeafBirch", "LeafSpruce", "LeafJungle", "LeafAutumn"})
            if (forest.palette[m] == leaf) fsp.cards.set(uint8_t(m));
    fsp.worldOriginVoxel = forest.originVoxel;
    fsp.threshold24 = sprigThreshold24(kSprigsPerM2, kVoxelsPerM);

    // Synthetic: random 3-material content with air pockets and card voxels, 70 x 45 x 33, chunk 24 x 16 x 20.
    VoxelGrid synth = box({70, 45, 33});
    std::mt19937 rng(17);
    for (size_t i = 0; i < synth.size(); ++i) {
        const glm::ivec3 p = synth.pos(i);
        const unsigned r = rng() % 100;
        synth.mat[i] = p.y < 20 ? (r < 90 ? 1 : 0) : (r < 20 ? 3 : r < 35 ? 2 : 0);
    }
    SprigParams ssp;
    ssp.cards.set(2);
    ssp.worldOriginVoxel = {-500, 64, 31};
    ssp.threshold24 = 1u << 23;

    struct Case { const char* name; const VoxelGrid* g; const SprigParams* sp; glm::ivec3 cs; Outside beyond; };
    const Case cases[] = {{"forest 96^3 chunks", &forest.grid, &fsp, {96, 96, 96}, Outside::Skip},
                          {"synthetic 24x16x20, beyond air", &synth, &ssp, {24, 16, 20}, Outside::Air},
                          {"synthetic 24x16x20, beyond skip", &synth, &ssp, {24, 16, 20}, Outside::Skip}};
    for (const Case& c : cases) {
        std::vector<UnitFace> whole, chunked;
        std::vector<std::tuple<int, int, int, int>> sprWhole, sprChunked;
        {
            std::vector<PackedFace> wf;
            for (const Quad& q : meshCulled(*c.g, c.beyond, &c.sp->cards)) wf.push_back(packFace(q));
            for (const PackedFace& f : foliageSprigs(*c.g, c.beyond, *c.sp)) wf.push_back(f);
            addUnitFaces(wf, {0, 0, 0}, whole, sprWhole);
        }
        int chunks = 0;
        for (int x = 0; x < c.g->dim.x; x += c.cs.x)
            for (int y = 0; y < c.g->dim.y; y += c.cs.y)
                for (int z = 0; z < c.g->dim.z; z += c.cs.z) {
                    const glm::ivec3 o(x, y, z), size = glm::min(c.cs, c.g->dim - o);
                    const VoxelGrid sub = subGrid(*c.g, o, size);
                    const Neighbours nb = neighboursOf(*c.g, o, size, c.beyond);
                    SprigParams sp = *c.sp;
                    sp.worldOriginVoxel = c.sp->worldOriginVoxel + o;
                    std::vector<PackedFace> f;
                    std::string err;
                    ASSERT_TRUE(gpu.build(sub, nb, f, &err, &sp)) << c.name << ": " << err;
                    const auto cpu = keys(cpuFaces(sub, nb, &sp)), got = keys(f);
                    EXPECT_TRUE(cpu == got) << c.name << " chunk (" << x << "," << y << "," << z
                                            << ") GPU vs CPU: " << firstDiff(cpu, got);
                    addUnitFaces(f, o, chunked, sprChunked);
                    ++chunks;
                }
        std::sort(whole.begin(), whole.end());
        std::sort(chunked.begin(), chunked.end());
        std::sort(sprWhole.begin(), sprWhole.end());
        std::sort(sprChunked.begin(), sprChunked.end());
        EXPECT_EQ(chunked.size(), whole.size()) << c.name;
        EXPECT_TRUE(chunked == whole) << c.name << ": GPU chunk faces differ from the whole region";
        EXPECT_TRUE(sprChunked == sprWhole) << c.name << ": sprigs " << sprChunked.size() << " vs " << sprWhole.size();
        EXPECT_GT(sprWhole.size(), 0u) << c.name;
        std::printf("[facebuilder] chunks invisible, %s: %d chunks, %zu unit faces, %zu sprigs\n", c.name, chunks,
                    whole.size(), sprWhole.size());
    }
}

TEST_F(VoxFoundFaceBuilderGpu, RefusesNeighboursOfTheWrongSize) {
    VoxelGrid g = box({8, 8, 8});
    Neighbours n;
    n.resize({8, 8, 9}, false);
    std::vector<PackedFace> f;
    std::string err;
    EXPECT_FALSE(gpu.build(g, n, f, &err));
    EXPECT_NE(err.find("Neighbours"), std::string::npos) << err;
}

// ---- F2.11: the GPU draw-building pass (voxfound_cull.comp) equals the CPU reference -----------------------------
// Red first: record() was a stub (no dispatch), so every command kept its full instance count.
TEST_F(VoxFoundFaceBuilderGpu, ClusterCullEqualsTheReference) {
    ClusterCullGpu cull;
    std::string err;
    ASSERT_TRUE(cull.init(device, physicalDevice, "shaders", 2, &err)) << err;
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> pos(-200.0f, 200.0f), ext(0.0f, 24.0f);
    std::uniform_int_distribution<int> cnt(1, 500), chunkOf(0, 6), coin(0, 3);
    std::vector<ClusterCullGpu::Cluster> cl(5000);
    uint32_t first = 0;
    for (auto& c : cl) {
        c.lo = glm::vec3(pos(rng), pos(rng) * 0.25f, pos(rng));
        c.hi = c.lo + glm::vec3(ext(rng), ext(rng), ext(rng));
        c.count = uint32_t(cnt(rng));
        c.firstInstance = first;
        first += c.count;
        c.chunk = uint32_t(chunkOf(rng));
    }
    ASSERT_TRUE(cull.upload(cl, 36u, 7u, queue, queueFamilyIndex, &err)) << err;
    for (uint32_t slot = 0; slot < 2; ++slot) {
        // a real camera: perspective, looking from (10, 30, 80) towards (-40, 0, -60), reverse-Z like the engine
        const glm::mat4 proj = glm::perspective(glm::radians(70.0f), 16.0f / 9.0f, 0.1f, 300.0f);
        const glm::vec3 eye = slot == 0 ? glm::vec3(10, 30, 80) : glm::vec3(-90, 10, -20);
        const glm::mat4 view = glm::lookAt(eye, glm::vec3(-40, 0, -60), glm::vec3(0, 1, 0));
        Utils::Frustum fr;
        fr.extractFromMatrix(proj * view, Utils::Frustum::ClipConvention::ForwardZeroToOne);
        glm::vec4 planes[6];
        for (int k = 0; k < 6; ++k) planes[k] = glm::vec4(fr.planes[size_t(k)].normal, fr.planes[size_t(k)].distance);
        std::vector<uint32_t> mask(7);
        for (auto& m : mask) m = coin(rng) != 0 ? 1u : 0u;
        cull.setChunkVisible(slot, mask);
        ASSERT_TRUE(cull.cullNow(queue, queueFamilyIndex, slot, planes, &err)) << err;
        std::vector<uint32_t> cmdv;
        ASSERT_TRUE(cull.readCommands(queue, queueFamilyIndex, slot, cmdv, &err)) << err;
        const uint32_t* cmd = cmdv.data();
        ASSERT_NE(cmd, nullptr);
        size_t visible = 0, bad = 0, nearEdge = 0;
        for (size_t i = 0; i < cl.size(); ++i) {
            EXPECT_EQ(cmd[i * 5 + 0], 36u);
            EXPECT_EQ(cmd[i * 5 + 4], cl[i].firstInstance);
            const bool want = mask[cl[i].chunk] != 0 && boxInFrustum(planes, cl[i].lo, cl[i].hi);
            const uint32_t expected = want ? cl[i].count : 0u;
            visible += want;
            if (cmd[i * 5 + 1] == expected) continue;
            // only a box within 1 mm of a plane may differ (fused multiply-add on the device)
            float closest = 1e30f;
            for (int k = 0; k < 6; ++k) {
                const glm::vec3 n(planes[k]);
                const glm::vec3 p(n.x >= 0 ? cl[i].hi.x : cl[i].lo.x, n.y >= 0 ? cl[i].hi.y : cl[i].lo.y,
                                  n.z >= 0 ? cl[i].hi.z : cl[i].lo.z);
                closest = std::min(closest, std::abs(glm::dot(n, p) + planes[k].w));
            }
            (closest < 1e-3f ? nearEdge : bad)++;
        }
        std::printf("[cluster cull] slot %u: %zu of %zu clusters visible, %zu wrong, %zu differ within 1 mm of a plane\n",
                    slot, visible, cl.size(), bad, nearEdge);
        EXPECT_GT(visible, 100u) << "the camera sees part of the field";
        EXPECT_LT(visible, cl.size() - 100) << "and culls part of it";
        EXPECT_EQ(bad, 0u);
    }
}

// ---- F2.14: the cull pass draws a cluster of level k only where a fragment in its box may keep level k -----------
// Red first: the F2.11 shader ignores the level (every cluster drawn), and must draw only level 0 without the test.
TEST_F(VoxFoundFaceBuilderGpu, ClusterCullDropsDirectionGroupsLikeTheReference) {
    // F2.15 (section 4.6 item 8): with the direction test on, a command keeps its count exactly when the frustum keeps
    // the cluster AND Mesher.h faceGroupMayFaceCamera does (clusters with no direction ignore the test)
    ClusterCullGpu cull;
    std::string err;
    ASSERT_TRUE(cull.init(device, physicalDevice, "shaders", 1, &err)) << err;
    std::mt19937 rng(23);
    std::uniform_real_distribution<float> pos(-100.0f, 100.0f), ext(0.0f, 16.0f);
    std::uniform_int_distribution<int> cnt(1, 300), dirOf(-1, 11);   // 6-11: F2.15d-2 away groups
    std::vector<ClusterCullGpu::Cluster> cl(6000);
    uint32_t first = 0;
    for (auto& c : cl) {
        c.lo = glm::vec3(pos(rng), pos(rng) * 0.25f, pos(rng));
        c.hi = c.lo + glm::vec3(ext(rng), ext(rng), ext(rng));
        c.count = uint32_t(cnt(rng));
        c.firstInstance = first;
        first += c.count;
        c.dir = dirOf(rng);
    }
    ASSERT_TRUE(cull.upload(cl, 36u, 1u, queue, queueFamilyIndex, &err)) << err;
    cull.setChunkVisible(0, {1u});
    glm::vec4 planes[6];
    for (auto& p : planes) p = glm::vec4(0.0f, 0.0f, 0.0f, 1e6f);   // everything inside: isolate the direction test
    const glm::vec3 cam(3.5f, 2.0f, -7.25f);
    ASSERT_TRUE(cull.cullNow(queue, queueFamilyIndex, 0, planes, &err, nullptr, &cam)) << err;
    std::vector<uint32_t> cmdv;
    ASSERT_TRUE(cull.readCommands(queue, queueFamilyIndex, 0, cmdv, &err)) << err;
    size_t dropped = 0, bad = 0;
    for (size_t i = 0; i < cl.size(); ++i) {
        const bool want = faceGroupMayFaceCamera(cl[i].dir, cl[i].lo, cl[i].hi, cam);
        dropped += !want;
        if (cmdv[i * 5 + 1] != (want ? cl[i].count : 0u)) ++bad;
    }
    std::printf("[cluster cull] direction test: %zu of %zu clusters dropped, %zu wrong\n", dropped, cl.size(), bad);
    EXPECT_GT(dropped, cl.size() / 5);
    EXPECT_EQ(bad, 0u);
    // F2.15b: the light mode -- a group is kept iff its faces face the light (Mesher.h faceDirFacesLight)
    const glm::vec3 toLight = glm::normalize(glm::vec3(-0.4f, 0.8f, 0.45f));
    ASSERT_TRUE(cull.cullNow(queue, queueFamilyIndex, 0, planes, &err, nullptr, &toLight, true)) << err;
    ASSERT_TRUE(cull.readCommands(queue, queueFamilyIndex, 0, cmdv, &err)) << err;
    size_t lightDropped = 0, lightBad = 0;
    for (size_t i = 0; i < cl.size(); ++i) {
        const bool want = faceDirFacesLight(cl[i].dir, toLight);
        lightDropped += !want;
        if (cmdv[i * 5 + 1] != (want ? cl[i].count : 0u)) ++lightBad;
    }
    std::printf("[cluster cull] light test: %zu of %zu clusters dropped, %zu wrong\n", lightDropped, cl.size(), lightBad);
    EXPECT_GT(lightDropped, cl.size() / 3);
    EXPECT_EQ(lightBad, 0u);
    // the test off: nothing dropped
    ASSERT_TRUE(cull.cullNow(queue, queueFamilyIndex, 0, planes, &err)) << err;
    ASSERT_TRUE(cull.readCommands(queue, queueFamilyIndex, 0, cmdv, &err)) << err;
    size_t off = 0;
    for (size_t i = 0; i < cl.size(); ++i) off += cmdv[i * 5 + 1] != cl[i].count;
    EXPECT_EQ(off, 0u);
}

TEST_F(VoxFoundFaceBuilderGpu, ClusterCullPicksLevelsLikeTheReference) {
    ClusterCullGpu cull;
    std::string err;
    ASSERT_TRUE(cull.init(device, physicalDevice, "shaders", 1, &err)) << err;
    std::mt19937 rng(214);
    std::uniform_real_distribution<float> pos(-300.0f, 300.0f), ext(0.5f, 8.0f);
    std::uniform_int_distribution<int> lvl(0, 3);
    std::vector<ClusterCullGpu::Cluster> cl(4000);
    for (size_t i = 0; i < cl.size(); ++i) {
        cl[i].lo = glm::vec3(pos(rng), pos(rng) * 0.2f, pos(rng));
        cl[i].hi = cl[i].lo + glm::vec3(ext(rng), ext(rng), ext(rng));
        cl[i].count = 7;
        cl[i].firstInstance = uint32_t(i * 7);
        cl[i].chunk = 0;
        cl[i].level = uint32_t(lvl(rng));
    }
    ASSERT_TRUE(cull.upload(cl, 36u, 1u, queue, queueFamilyIndex, &err)) << err;
    cull.setChunkVisible(0, {1u});
    glm::vec4 all[6];
    for (auto& p : all) p = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);   // every plane passes: only the level decides
    std::vector<uint32_t> cmdv;
    ASSERT_TRUE(cull.cullNow(queue, queueFamilyIndex, 0, all, &err)) << err;
    ASSERT_TRUE(cull.readCommands(queue, queueFamilyIndex, 0, cmdv, &err)) << err;
    for (size_t i = 0; i < cl.size(); ++i)
        ASSERT_EQ(cmdv[i * 5 + 1], cl[i].level == 0 ? 7u : 0u) << "without the level test only level 0 draws";
    ClusterCullGpu::LodCull lod;
    lod.cam = glm::vec3(12.0f, 25.0f, -7.0f);
    lod.c = lodScaleFromCode(lodScaleCode(2.0f, 870.0f));
    lod.maxLevel = 3;
    ASSERT_TRUE(cull.cullNow(queue, queueFamilyIndex, 0, all, &err, &lod)) << err;
    ASSERT_TRUE(cull.readCommands(queue, queueFamilyIndex, 0, cmdv, &err)) << err;
    size_t drawn = 0, bad = 0, nearSwitch = 0;
    int perLevel[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < cl.size(); ++i) {
        const glm::ivec2 r = lodLevelsForBox(cl[i].lo, cl[i].hi, lod.cam, lod.c, 3);
        const bool want = int(cl[i].level) >= r.x && int(cl[i].level) <= r.y;
        drawn += want;
        perLevel[cl[i].level] += want;
        if (cmdv[i * 5 + 1] == (want ? 7u : 0u)) continue;
        // only a box whose range ends within 1e-4 levels of a switch may differ (log2 on the device)
        const glm::ivec2 a = lodLevelsForBox(cl[i].lo, cl[i].hi, lod.cam, lod.c - 1e-4f, 3),
                         b = lodLevelsForBox(cl[i].lo, cl[i].hi, lod.cam, lod.c + 1e-4f, 3);
        (a != b ? nearSwitch : bad)++;
    }
    std::printf("[cluster lod] %zu of %zu drawn (per level %d %d %d %d), %zu wrong, %zu within 1e-4 of a switch\n",
                drawn, cl.size(), perLevel[0], perLevel[1], perLevel[2], perLevel[3], bad, nearSwitch);
    for (int k = 0; k < 4; ++k) EXPECT_GT(perLevel[k], 20) << "every level is drawn somewhere: " << k;
    EXPECT_LT(drawn, cl.size() * 3 / 4) << "and most clusters of the wrong level are not";
    EXPECT_EQ(bad, 0u);
}

// ---- F2.15f (section 4.6 item 8's last part): compacted + merged commands ----------------------------------------
// The cull pass appends only the visible clusters, merging consecutive ones of the same chunk, level and multidraw
// whose faces are contiguous (Mesher.h mergeVisibleRuns), each with its chunk origin. Red first: a stub that writes
// no compacted commands.
TEST_F(VoxFoundFaceBuilderGpu, CompactedCommandsDrawTheSameFacesAsPerClusterCommands) {
    ClusterCullGpu cull;
    std::string err;
    ASSERT_TRUE(cull.init(device, physicalDevice, "shaders", 2, &err)) << err;
    std::mt19937 rng(2151);
    std::uniform_real_distribution<float> pos(-60.0f, 60.0f), ext(0.0f, 6.0f), u01(0.0f, 1.0f);
    std::uniform_int_distribution<int> cnt(1, 40), dirOf(-1, 5), chunkOf(0, 5), lvl(0, 2);
    const uint32_t n = 5000, solid = 3700;
    std::vector<ClusterCullGpu::Cluster> cl(n);
    std::vector<glm::ivec4> origins(n);
    uint32_t first = 0, chunk = 0, level = 0;
    for (uint32_t i = 0; i < n; ++i) {
        auto& c = cl[i];
        if (u01(rng) < 0.08f) chunk = uint32_t(chunkOf(rng));    // runs of one chunk, sometimes a new one
        if (u01(rng) < 0.05f) level = uint32_t(lvl(rng));
        if (u01(rng) < 0.04f) first += uint32_t(cnt(rng));       // a broken face range
        c.lo = glm::vec3(pos(rng), pos(rng) * 0.3f, pos(rng));
        c.hi = c.lo + glm::vec3(ext(rng), ext(rng), ext(rng));
        c.count = uint32_t(cnt(rng));
        c.firstInstance = first;
        first += c.count;
        c.chunk = chunk;
        c.level = level;
        c.dir = dirOf(rng);
        origins[i] = glm::ivec4(int(chunk) * 32, -int(chunk), int(chunk) * 7 + 1, int(level) * 3 + 5);
    }
    ClusterCullGpu::Compact compact{origins, solid};
    ASSERT_TRUE(cull.upload(cl, 36u, 6u, queue, queueFamilyIndex, &err, &compact)) << err;
    cull.setChunkVisible(1, {1u, 1u, 0u, 1u, 1u, 1u});   // chunk 2 culled by the CPU's per-chunk decision
    // most boxes inside, the side planes still cut some; the camera's octant drops about half the direction groups
    const glm::mat4 proj = glm::perspective(glm::radians(80.0f), 1.6f, 0.1f, 300.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(10.0f, 30.0f, -90.0f), glm::vec3(0.0f), glm::vec3(0, 1, 0));
    glm::vec4 planes[6];
    clipPlanesZeroToOne(proj * view, glm::vec3(0.0f), planes);
    const glm::vec3 cam(10.0f, 30.0f, -90.0f);
    // the reference: today's per-cluster commands
    ASSERT_TRUE(cull.cullNow(queue, queueFamilyIndex, 1, planes, &err, nullptr, &cam)) << err;
    std::vector<uint32_t> perCluster;
    ASSERT_TRUE(cull.readCommands(queue, queueFamilyIndex, 1, perCluster, &err)) << err;
    std::vector<uint8_t> vis(n);
    std::vector<uint32_t> firsts(n), counts(n), keys(n);
    std::vector<std::pair<uint32_t, glm::ivec4>> want;   // (face index, origin) drawn
    for (uint32_t i = 0; i < n; ++i) {
        vis[i] = perCluster[i * 5 + 1] != 0u;
        firsts[i] = cl[i].firstInstance;
        counts[i] = cl[i].count;
        keys[i] = cl[i].chunk | cl[i].level << 24;
        if (vis[i])
            for (uint32_t f = 0; f < cl[i].count; ++f) want.push_back({cl[i].firstInstance + f, origins[i]});
    }
    const std::vector<DrawRun> ref = mergeVisibleRuns(firsts, counts, keys, vis, solid, ClusterCullGpu::kMergeBlock);
    // the compacted pass
    ASSERT_TRUE(cull.cullNow(queue, queueFamilyIndex, 1, planes, &err, nullptr, &cam, false, true)) << err;
    std::vector<uint32_t> cmds;
    std::vector<glm::ivec4> drawOrigins;
    uint32_t drawn[2] = {0, 0};
    ASSERT_TRUE(cull.readCompact(queue, queueFamilyIndex, 1, cmds, drawOrigins, drawn, &err)) << err;
    std::vector<std::pair<uint32_t, glm::ivec4>> got;
    std::vector<std::tuple<uint32_t, uint32_t, bool>> gotRuns, refRuns;
    size_t badStatic = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (uint32_t k = 0; k < drawn[pass]; ++k) {
            const uint32_t s = pass ? solid + k : k;
            if (cmds[s * 5 + 0] != 36u || cmds[s * 5 + 2] != 0u || cmds[s * 5 + 3] != 0u) ++badStatic;
            const uint32_t o = pass ? cull.leafOriginBase() + k : k;   // the leaf multidraw's origins: own base
            gotRuns.emplace_back(cmds[s * 5 + 4], cmds[s * 5 + 1], pass == 1);
            for (uint32_t f = 0; f < cmds[s * 5 + 1]; ++f) got.push_back({cmds[s * 5 + 4] + f, drawOrigins[o]});
        }
    for (const DrawRun& r : ref) refRuns.emplace_back(r.first, r.count, r.cluster >= solid);
    auto lessPair = [](const auto& a, const auto& b) {
        return std::tie(a.first, a.second.x, a.second.y, a.second.z, a.second.w) <
               std::tie(b.first, b.second.x, b.second.y, b.second.z, b.second.w);
    };
    std::sort(want.begin(), want.end(), lessPair);
    std::sort(got.begin(), got.end(), lessPair);
    // F2.15f-2: IN ORDER -- the cluster order is the draw order the per-cluster path had (an atomic append scrambled
    // it and cost +0.7-1.0 ms of overdraw at the close poses, voxfound_f215f_ab.txt)
    size_t visible = 0;
    for (uint8_t v : vis) visible += v;
    std::printf("[compact] %u clusters, %zu visible -> %u + %u commands (reference %zu), %zu faces\n", n, visible,
                drawn[0], drawn[1], ref.size(), want.size());
    EXPECT_GT(visible, n / 10);
    EXPECT_LT(visible, n * 9 / 10);
    EXPECT_EQ(badStatic, 0u);
    EXPECT_EQ(gotRuns, refRuns);
    EXPECT_TRUE(got == want) << "faces/origins drawn differ: " << got.size() << " vs " << want.size();
    EXPECT_LT(ref.size(), visible);   // merging happened
    EXPECT_TRUE(std::any_of(ref.begin(), ref.end(), [&](const DrawRun& r) { return r.cluster >= solid; }));
    EXPECT_TRUE(std::any_of(ref.begin(), ref.end(), [&](const DrawRun& r) { return r.cluster < solid; }));
    // the reference never merges across a chunk, a level, the multidraw split or a face gap, nor past 32 clusters
    for (const DrawRun& r : ref) {
        uint32_t i = r.cluster, sum = 0;
        for (; i < n && sum < r.count; ++i) sum += counts[i];
        const uint32_t last = i - 1;
        EXPECT_EQ(sum, r.count);
        EXPECT_EQ(r.cluster / ClusterCullGpu::kMergeBlock, last / ClusterCullGpu::kMergeBlock);
        for (uint32_t j = r.cluster + 1; j <= last; ++j) {
            EXPECT_EQ(keys[j], keys[r.cluster]);
            EXPECT_EQ(firsts[j], firsts[j - 1] + counts[j - 1]);
            EXPECT_EQ(j >= solid, r.cluster >= solid);
            EXPECT_TRUE(vis[j]);
        }
    }
    // the per-cluster commands survive a compacted pass (the uncompacted path reads their static words; the first
    // version appended into the same buffer and broke it -- black ground at canopy_close)
    ASSERT_TRUE(cull.cullNow(queue, queueFamilyIndex, 1, planes, &err, nullptr, &cam)) << err;
    std::vector<uint32_t> perClusterAgain;
    ASSERT_TRUE(cull.readCommands(queue, queueFamilyIndex, 1, perClusterAgain, &err)) << err;
    EXPECT_TRUE(perClusterAgain == perCluster);
    // a second slot never sees the first's counts (each slot resets its own)
    ASSERT_TRUE(cull.cullNow(queue, queueFamilyIndex, 0, planes, &err, nullptr, &cam, false, true)) << err;
    uint32_t drawn0[2] = {0, 0};
    ASSERT_TRUE(cull.readCompact(queue, queueFamilyIndex, 0, cmds, drawOrigins, drawn0, &err)) << err;
    ASSERT_TRUE(cull.cullNow(queue, queueFamilyIndex, 1, planes, &err, nullptr, &cam, false, true)) << err;
    uint32_t again[2] = {0, 0};
    ASSERT_TRUE(cull.readCompact(queue, queueFamilyIndex, 1, cmds, drawOrigins, again, &err)) << err;
    EXPECT_EQ(again[0], drawn[0]);
    EXPECT_EQ(again[1], drawn[1]);
}

// F2.16 (§4.6 item 9): the device form of the incremental rebuild. 12 grids (awkward sizes, one a full 256 side, real
// border planes) are edited 240 times in random order, so the 8 resident slots hit, miss and evict; edits are boxes,
// spheres and border-plane-only changes. Each edit: the CPU plan, then the device rows (rowsDelta, only the edit box
// uploaded) must equal the CPU executor of the same plan as a set; the kept faces + added = a full meshRuns at the end.
TEST_F(VoxFoundFaceBuilderGpu, RowsDeltaEqualsTheCpuPlan) {
    std::mt19937 rng(20216u);
    auto rnd = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    CardSet cards;
    cards.set(4);
    struct Grid {
        VoxelGrid g;
        Neighbours nb;
        std::vector<PackedFace> faces;
    };
    std::vector<Grid> grids;
    const glm::ivec3 dims[] = {{40, 24, 48}, {33, 17, 9}, {256, 40, 32}, {16, 64, 16}, {48, 48, 48}, {7, 31, 64},
                               {64, 24, 40}, {24, 24, 24}, {72, 16, 56}, {32, 32, 32}, {9, 9, 120}, {56, 40, 24}};
    for (const glm::ivec3 d : dims) {
        Grid G;
        G.g.resize(d);
        for (int x = 0; x < d.x; ++x)
            for (int z = 0; z < d.z; ++z) {
                const int h = d.y / 3 + rnd(0, std::max(1, d.y / 4));
                for (int y = 0; y < d.y; ++y) {
                    uint8_t m = 0;
                    if (y < h - 3) m = 1;
                    else if (y < h - 1) m = 2;
                    else if (y < h) m = 3;
                    else if (rnd(0, 99) < 6) m = uint8_t(rnd(1, 4));
                    G.g.set({x, y, z}, m);
                }
            }
        G.nb.resize(d, false);
        for (int a = 0; a < 3; ++a)
            for (int s : {-1, 1})
                for (int v = 0; v < d[(a + 2) % 3]; ++v)
                    for (int u = 0; u < d[(a + 1) % 3]; ++u)
                        if (rnd(0, 1)) G.nb.set(a, s, u, v, true);
        for (const Quad& q : meshRuns(G.g, Edges(G.nb), &cards)) G.faces.push_back(packFace(q));
        grids.push_back(std::move(G));
    }
    size_t rows = 0, added = 0, hits = 0;
    for (int it = 0; it < 240; ++it) {
        const size_t key = size_t(rnd(0, int(grids.size()) - 1));
        Grid& G = grids[key];
        const glm::ivec3 d = G.g.dim;
        const int kind = rnd(0, 9);
        const uint8_t m = rnd(0, 99) < 35 ? 0 : uint8_t(rnd(1, 4));
        glm::ivec3 lo, hi;
        if (kind == 9) {   // a border plane alone
            const int a = rnd(0, 2), s = rnd(0, 1) ? 1 : -1, ua = (a + 1) % 3, va = (a + 2) % 3;
            const int u0 = rnd(0, d[ua] - 1), v0 = rnd(0, d[va] - 1);
            const int u1 = std::min(d[ua] - 1, u0 + rnd(0, d[ua] / 3));
            const int v1 = std::min(d[va] - 1, v0 + rnd(0, d[va] / 3));
            const bool solid = rnd(0, 1) != 0;
            for (int v = v0; v <= v1; ++v)
                for (int u = u0; u <= u1; ++u) G.nb.set(a, s, u, v, solid);
            lo[a] = hi[a] = s < 0 ? -1 : d[a];
            lo[ua] = u0;
            hi[ua] = u1;
            lo[va] = v0;
            hi[va] = v1;
        } else if (kind >= 7) {
            const glm::ivec3 c(rnd(0, d.x - 1), rnd(0, d.y - 1), rnd(0, d.z - 1));
            const int r = rnd(0, std::max(1, std::max({d.x, d.y, d.z}) / 6));
            lo = glm::max(c - r, glm::ivec3(0));
            hi = glm::min(c + r, d - 1);
            for (int x = lo.x; x <= hi.x; ++x)
                for (int y = lo.y; y <= hi.y; ++y)
                    for (int z = lo.z; z <= hi.z; ++z)
                        if (glm::length(glm::vec3(x, y, z) - glm::vec3(c)) <= float(r) + 0.5f) G.g.set({x, y, z}, m);
        } else {
            for (int a = 0; a < 3; ++a) {
                lo[a] = rnd(-1, d[a]);
                hi[a] = std::min(d[a], lo[a] + rnd(0, std::max(1, d[a] / 3)));
            }
            for (int x = std::max(lo.x, 0); x <= std::min(hi.x, d.x - 1); ++x)
                for (int y = std::max(lo.y, 0); y <= std::min(hi.y, d.y - 1); ++y)
                    for (int z = std::max(lo.z, 0); z <= std::min(hi.z, d.z - 1); ++z) G.g.set({x, y, z}, m);
        }
        const RunsPlan plan = planRunsDelta(d, G.faces, lo, hi);
        const RunsDelta cpu = meshRunsPlan(G.g, Edges(G.nb), plan, &cards);
        std::vector<PackedFace> dev;
        std::string err;
        ASSERT_TRUE(gpu.rowsDelta(uint64_t(key), G.g, G.nb, lo, hi, plan, &cards, dev, &err)) << err;
        hits += gpu.lastResidentHit() ? 1 : 0;
        const auto c = keys(cpu.added), k = keys(dev);
        ASSERT_EQ(c, k) << "edit " << it << " grid " << key << " kind " << kind << ": " << firstDiff(c, k);
        rows += plan.rows();
        added += dev.size();
        std::vector<char> gone(G.faces.size(), 0);
        for (uint32_t i : plan.removed) gone[i] = 1;
        std::vector<PackedFace> next;
        for (size_t i = 0; i < G.faces.size(); ++i)
            if (!gone[i]) next.push_back(G.faces[i]);
        next.insert(next.end(), dev.begin(), dev.end());
        G.faces.swap(next);
    }
    for (size_t i = 0; i < grids.size(); ++i) {
        std::vector<PackedFace> full;
        for (const Quad& q : meshRuns(grids[i].g, Edges(grids[i].nb), &cards)) full.push_back(packFace(q));
        EXPECT_EQ(keys(grids[i].faces), keys(full)) << "grid " << i << " after the edits";
    }
    EXPECT_GT(hits, 100u) << "resident slots should hit most of the time";
    EXPECT_LT(hits, 240u) << "12 grids over 8 slots must miss sometimes";
    std::printf("[rowsDelta] 240 edits over 12 grids: %zu rows, %zu faces added, %zu resident hits\n", rows, added, hits);
}

// D4 follow-up (glass): CLEAR materials (CardSet::setClear) -- the GPU builder (whole builds with Neighbours that
// carry clear planes, and the row-list delta) equals meshRuns' face rule (Mesher.h: q hides p iff q solid and not
// (q clear and p opaque)), incl. clear material 255 (the high word) and the uniform-block skip (a block of glass
// no longer "hides everything").
TEST_F(VoxFoundFaceBuilderGpu, ClearVoxelsEqualTheReference) {
    std::mt19937 rng(4417u);
    auto rnd = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    SprigParams sp;
    sp.cards.set(2);
    sp.cards.setClear(3);
    sp.cards.setClear(255);
    sp.worldOriginVoxel = {11, -40, 900};
    sp.threshold24 = sprigThreshold24(kSprigsPerM2, kVoxelsPerM);
    const glm::ivec3 dims[] = {{24, 20, 28}, {33, 65, 17}, {64, 40, 48}, {256, 9, 5}};
    size_t clearFaces = 0, gridIndex = 0;
    for (const glm::ivec3 d : dims) {
        ++gridIndex;   // the resident key per grid (a fresh key = a full voxel upload)
        VoxelGrid g = box(d);
        for (int x = 0; x < d.x; ++x)
            for (int y = 0; y < d.y; ++y)
                for (int z = 0; z < d.z; ++z) {
                    const unsigned r = unsigned(rnd(0, 99));
                    uint8_t m = r < 30 ? 1 : r < 40 ? 2 : r < 55 ? 3 : r < 58 ? 255 : 0;
                    if (x < 16 && y < 16 && z < 16) m = 3;   // a whole 8^3-aligned glass region (the block skip)
                    g.set({x, y, z}, m);
                }
        Neighbours nb;
        nb.resize(d, false);
        for (int a = 0; a < 3; ++a)
            for (int s : {-1, 1})
                for (int v = 0; v < d[(a + 2) % 3]; ++v)
                    for (int u = 0; u < d[(a + 1) % 3]; ++u) {
                        const int r = rnd(0, 2);   // 0 air, 1 opaque, 2 clear
                        if (r) nb.set(a, s, u, v, true);
                        if (r == 2) nb.setClear(a, s, u, v, true);
                    }
        const std::string name = std::to_string(d.x) + "x" + std::to_string(d.y) + "x" + std::to_string(d.z);
        for (bool skip : {false, true}) {
            gpu.setUniformBlockSkip(skip);
            same(g, Edges(nb), name + (skip ? " block skip" : ""), &sp);
        }
        for (const Quad& q : meshRuns(g, Edges(nb), &sp.cards)) clearFaces += q.material == 3 || q.material == 255;
        // the delta: a few edits, each plan re-meshed on the device == the CPU plan
        std::vector<PackedFace> faces;
        for (const Quad& q : meshRuns(g, Edges(nb), &sp.cards)) faces.push_back(packFace(q));
        for (int it = 0; it < 12; ++it) {
            const glm::ivec3 c(rnd(0, d.x - 1), rnd(0, d.y - 1), rnd(0, d.z - 1));
            const glm::ivec3 lo = glm::max(c - 3, glm::ivec3(0)), hi = glm::min(c + 3, d - 1);
            const uint8_t m = uint8_t(std::array<int, 4>{0, 1, 3, 255}[size_t(rnd(0, 3))]);
            for (int x = lo.x; x <= hi.x; ++x)
                for (int y = lo.y; y <= hi.y; ++y)
                    for (int z = lo.z; z <= hi.z; ++z) g.set({x, y, z}, m);
            const RunsPlan plan = planRunsDelta(d, faces, lo, hi);
            const RunsDelta cpu = meshRunsPlan(g, Edges(nb), plan, &sp.cards);
            std::vector<PackedFace> dev;
            std::string err;
            ASSERT_TRUE(gpu.rowsDelta(9000u + uint64_t(gridIndex), g, nb, lo, hi, plan, &sp.cards, dev, &err)) << err;
            ASSERT_EQ(keys(cpu.added), keys(dev)) << name << " edit " << it << ": " << firstDiff(keys(cpu.added), keys(dev));
            std::vector<char> gone(faces.size(), 0);
            for (uint32_t i : plan.removed) gone[i] = 1;
            std::vector<PackedFace> next;
            for (size_t i = 0; i < faces.size(); ++i)
                if (!gone[i]) next.push_back(faces[i]);
            next.insert(next.end(), dev.begin(), dev.end());
            faces.swap(next);
        }
        std::vector<PackedFace> full;
        for (const Quad& q : meshRuns(g, Edges(nb), &sp.cards)) full.push_back(packFace(q));
        EXPECT_EQ(keys(faces), keys(full)) << name << " after the edits";
    }
    gpu.setUniformBlockSkip(true);
    EXPECT_GT(clearFaces, 1000u);
}

// F2.17 (§4.6 item 10): the cull tables updated in place. A table of 4,000 clusters (6 chunks, 3 levels, a solid/leaf
// split, broken face ranges) is rewritten 50 times over random slot ranges by recordUpdate (new boxes, counts, first
// faces, chunks and origins, and count-0 slots: the face pool's empty slots); after every round, culling it must give
// exactly what a FRESH upload() of the same table gives: the per-cluster commands, and the compacted commands, their
// origins and counts. A count-0 cluster is never drawn.
TEST_F(VoxFoundFaceBuilderGpu, PartialUpdateEqualsAFullUpload) {
    std::string err;
    ClusterCullGpu upd, ref;
    ASSERT_TRUE(upd.init(device, physicalDevice, "shaders", 2, &err)) << err;
    std::mt19937 rng(2171);
    std::uniform_real_distribution<float> pos(-60.0f, 60.0f), ext(0.0f, 6.0f), u01(0.0f, 1.0f);
    std::uniform_int_distribution<int> cnt(1, 40), dirOf(-1, 5), chunkOf(0, 5), lvl(0, 2);
    const uint32_t n = 4000, solid = 3100;
    std::vector<ClusterCullGpu::Cluster> cl(n);
    std::vector<glm::ivec4> origins(n);
    uint32_t face = 0;
    auto make = [&](uint32_t i, uint32_t chunk, uint32_t level) {
        auto& c = cl[i];
        c.lo = glm::vec3(pos(rng), pos(rng) * 0.3f, pos(rng));
        c.hi = c.lo + glm::vec3(ext(rng), ext(rng), ext(rng));
        c.count = u01(rng) < 0.1f ? 0u : uint32_t(cnt(rng));   // empty slots too
        if (u01(rng) < 0.05f) face += uint32_t(cnt(rng));
        c.firstInstance = face;
        face += c.count;
        c.chunk = chunk;
        c.level = level;
        c.dir = dirOf(rng);
        origins[i] = glm::ivec4(int(chunk) * 32, -int(chunk), int(chunk) * 7 + 1, int(level) * 3 + 5);
    };
    {
        uint32_t chunk = 0, level = 0;
        for (uint32_t i = 0; i < n; ++i) {
            if (u01(rng) < 0.08f) chunk = uint32_t(chunkOf(rng));
            if (u01(rng) < 0.05f) level = uint32_t(lvl(rng));
            make(i, chunk, level);
        }
    }
    ClusterCullGpu::Compact compact{origins, solid};
    ASSERT_TRUE(upd.upload(cl, 36u, 6u, queue, queueFamilyIndex, &err, &compact)) << err;
    const glm::mat4 proj = glm::perspective(glm::radians(80.0f), 1.6f, 0.1f, 300.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(10.0f, 30.0f, -90.0f), glm::vec3(0.0f), glm::vec3(0, 1, 0));
    glm::vec4 planes[6];
    clipPlanesZeroToOne(proj * view, glm::vec3(0.0f), planes);
    const glm::vec3 cam(10.0f, 30.0f, -90.0f);
    const std::vector<uint32_t> mask = {1u, 1u, 0u, 1u, 1u, 1u};
    size_t updatedSlots = 0, compared = 0;
    for (int round = 0; round < 50; ++round) {
        SCOPED_TRACE("round " + std::to_string(round));
        // a random slot range, rewritten (a chunk span: one chunk and level, or empty slots)
        const uint32_t len = 1 + rng() % 300u, first = rng() % (n - len);
        const uint32_t chunk = uint32_t(chunkOf(rng)), level = uint32_t(lvl(rng));
        face = uint32_t(rng() % 50000u);
        for (uint32_t i = first; i < first + len; ++i) make(i, chunk, level);
        const std::vector<ClusterCullGpu::Cluster> part(cl.begin() + first, cl.begin() + first + len);
        const std::vector<glm::ivec4> partOrigins(origins.begin() + first, origins.begin() + first + len);
        ASSERT_TRUE(upd.updateNow(queue, queueFamilyIndex, uint32_t(round % 2), first, part, &partOrigins, &err)) << err;
        updatedSlots += len;
        ref.shutdown();
        ASSERT_TRUE(ref.init(device, physicalDevice, "shaders", 2, &err)) << err;
        ClusterCullGpu::Compact c2{origins, solid};
        ASSERT_TRUE(ref.upload(cl, 36u, 6u, queue, queueFamilyIndex, &err, &c2)) << err;
        for (ClusterCullGpu* g : {&upd, &ref}) g->setChunkVisible(1, mask);
        std::vector<uint32_t> a, b;
        ASSERT_TRUE(upd.cullNow(queue, queueFamilyIndex, 1, planes, &err, nullptr, &cam)) << err;
        ASSERT_TRUE(ref.cullNow(queue, queueFamilyIndex, 1, planes, &err, nullptr, &cam)) << err;
        ASSERT_TRUE(upd.readCommands(queue, queueFamilyIndex, 1, a, &err)) << err;
        ASSERT_TRUE(ref.readCommands(queue, queueFamilyIndex, 1, b, &err)) << err;
        ASSERT_EQ(a, b) << "per-cluster commands differ";
        for (uint32_t i = 0; i < n; ++i)
            if (cl[i].count == 0) ASSERT_EQ(a[i * 5 + 1], 0u) << "an empty slot is drawn: " << i;
        ASSERT_TRUE(upd.cullNow(queue, queueFamilyIndex, 1, planes, &err, nullptr, &cam, false, true)) << err;
        ASSERT_TRUE(ref.cullNow(queue, queueFamilyIndex, 1, planes, &err, nullptr, &cam, false, true)) << err;
        std::vector<uint32_t> ca, cb;
        std::vector<glm::ivec4> oa, ob;
        uint32_t na[2] = {0, 0}, nb[2] = {0, 0};
        ASSERT_TRUE(upd.readCompact(queue, queueFamilyIndex, 1, ca, oa, na, &err)) << err;
        ASSERT_TRUE(ref.readCompact(queue, queueFamilyIndex, 1, cb, ob, nb, &err)) << err;
        ASSERT_EQ(na[0], nb[0]);
        ASSERT_EQ(na[1], nb[1]);
        for (int pass = 0; pass < 2; ++pass)
            for (uint32_t k = 0; k < na[pass]; ++k) {
                const uint32_t s = pass ? solid + k : k;
                const uint32_t o = pass ? upd.leafOriginBase() + k : k;
                for (int w = 0; w < 5; ++w) ASSERT_EQ(ca[s * 5 + w], cb[s * 5 + w]) << "command " << s;
                ASSERT_EQ(oa[o], ob[o]) << "origin " << o;
                ++compared;
            }
    }
    std::printf("[partial] 50 rounds, %zu slots rewritten in place, %zu compacted commands compared\n", updatedSlots,
                compared);
    EXPECT_GT(compared, 1000u);
}

// F2.17: several recordUpdate calls in ONE command buffer (the renderer records one per dirty range in a frame), each
// larger than the last so the staging must grow mid-frame. The first live gate lost the device here: growing the
// staging destroyed the buffer the earlier copies of the same command buffer read from. The result must equal a fresh
// upload of the same table.
TEST_F(VoxFoundFaceBuilderGpu, SeveralUpdatesInOneCommandBuffer) {
    std::string err;
    ClusterCullGpu upd, ref;
    ASSERT_TRUE(upd.init(device, physicalDevice, "shaders", 2, &err)) << err;
    ASSERT_TRUE(ref.init(device, physicalDevice, "shaders", 2, &err)) << err;
    std::mt19937 rng(2172);
    std::uniform_real_distribution<float> pos(-60.0f, 60.0f), ext(0.0f, 6.0f);
    const uint32_t n = 12000;
    std::vector<ClusterCullGpu::Cluster> cl(n);
    std::vector<glm::ivec4> origins(n);
    auto make = [&](uint32_t i, uint32_t face) {
        cl[i] = {glm::vec3(pos(rng), pos(rng) * 0.3f, pos(rng)), glm::vec3(0.0f), face, 1u + rng() % 30u,
                 uint32_t(rng() % 4u), 0u, int(rng() % 7u) - 1};
        cl[i].hi = cl[i].lo + glm::vec3(ext(rng), ext(rng), ext(rng));
        origins[i] = glm::ivec4(int(cl[i].chunk) * 32, 0, 0, 5);
    };
    for (uint32_t i = 0; i < n; ++i) make(i, i * 40u);
    ClusterCullGpu::Compact compact{origins, n};
    ASSERT_TRUE(upd.upload(cl, 36u, 4u, queue, queueFamilyIndex, &err, &compact)) << err;
    // three ranges: 100, 1,500 and 6,000 slots (the staging starts at 64 KB = ~900 slots, then must grow twice)
    std::vector<ClusterCullGpu::UpdateBlock> blocks;
    uint32_t at = 0;
    for (uint32_t len : {100u, 1500u, 6000u}) {
        ClusterCullGpu::UpdateBlock b;
        b.first = at;
        for (uint32_t i = at; i < at + len; ++i) {
            make(i, 900000u + i * 40u);
            b.clusters.push_back(cl[i]);
            b.origins.push_back(origins[i]);
        }
        blocks.push_back(std::move(b));
        at += len + 200;
    }
    ASSERT_TRUE(upd.updateNow(queue, queueFamilyIndex, 1, blocks, &err)) << err;
    ClusterCullGpu::Compact c2{origins, n};
    ASSERT_TRUE(ref.upload(cl, 36u, 4u, queue, queueFamilyIndex, &err, &c2)) << err;
    const glm::mat4 proj = glm::perspective(glm::radians(80.0f), 1.6f, 0.1f, 300.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(10.0f, 30.0f, -90.0f), glm::vec3(0.0f), glm::vec3(0, 1, 0));
    glm::vec4 planes[6];
    clipPlanesZeroToOne(proj * view, glm::vec3(0.0f), planes);
    std::vector<uint32_t> a, b;
    ASSERT_TRUE(upd.cullNow(queue, queueFamilyIndex, 1, planes, &err)) << err;
    ASSERT_TRUE(ref.cullNow(queue, queueFamilyIndex, 1, planes, &err)) << err;
    ASSERT_TRUE(upd.readCommands(queue, queueFamilyIndex, 1, a, &err)) << err;
    ASSERT_TRUE(ref.readCommands(queue, queueFamilyIndex, 1, b, &err)) << err;
    EXPECT_EQ(a, b);
    // and the next frame on the same slot (the retired staging is released there)
    ASSERT_TRUE(upd.updateNow(queue, queueFamilyIndex, 1, {blocks[0]}, &err)) << err;
}
