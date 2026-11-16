// docs/VoxelFoundationPlan.md §4 + F2 — the CPU reference mesher (core/voxfound/Mesher.h).
//
// 1. Exact rigs with counts worked out by hand (written before the code ran).
// 2. Exactness: on random content and generated terrain, every merged mesh covers every exposed unit face
//    exactly once with the right material (coverageErrors == 0), at block limits 0 and 8.
// 3. MEASURED face counts (cost inputs, not the gate — the F2 gate is "a world that works today still works",
//    docs/VoxelFoundationPlan.md §3.5a point 4):
//    - terrain areas from the ENGINE's generator (WorldGenerator, fixed seeds) at 1/8 m, bare and with the
//      engine's flora plan stamped from the tree_forge --grid 8 library;
//    - "today proxy": the same area reduced to 1 m cubes (each metre takes the voxel at its centre), merged
//      greedily within 32 m chunks, which is what today's cube-face merge does (commit e916d1e);
//    - a two-story building shell realized by StructureRealizer on both grids (1/9 m canvas = today's fine
//      voxels, 1/8 m canvas = the foundation grid), both greedy-merged the same way.
//    No light in the merge key on either side: every count is a LOWER bound for a lit merge.
//    Predictions (written before the first run, docs/VoxelFoundationPlan.md F2):
//      bare Perlin footprint (32 x 32 m): culled 70k-200k unit faces, greedy 3k-15k quads, today proxy
//      0.5k-3k, so 1/8 m greedy is 3-10x the proxy; per-block merge (limit 8) 1.5-3x the unbounded merge;
//      building shell: the 1/8 m greedy count within +-30 % of the 1/9 m one.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <tuple>

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include "core/BuildingProgram.h"
#include "core/StructureGrid.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/WorldGenerator.h"
#include "core/voxfound/FacePool.h"
#include "core/voxfound/Mesher.h"
#include "core/voxfound/RegionEdit.h"
#include "core/voxfound/StorageLab.h"

using namespace Phyxel;
using namespace Phyxel::Core;
using namespace Phyxel::Core::VoxFound;

namespace {

VoxelGrid box(glm::ivec3 dim) {
    VoxelGrid g;
    g.resize(dim);
    return g;
}

std::string repoRoot() {
    namespace fs = std::filesystem;
    fs::path p = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        if (fs::exists(p / "resources" / "biomes.json") && fs::exists(p / "resources" / "templates_g8")) return p.string();
        if (!p.has_parent_path() || p.parent_path() == p) break;
        p = p.parent_path();
    }
    return {};
}

VoxelArea surfaceArea(WorldGenerator& gen, int x0, int z0, int headroomM = 0) {
    float lo = std::numeric_limits<float>::max(), hi = -lo;
    for (int x = x0 - 1; x <= x0 + 32; ++x)
        for (int z = z0 - 1; z <= z0 + 32; ++z) {
            const float h = gen.sampleSurface(x, z).bedHeight;
            lo = std::min(lo, h);
            hi = std::max(hi, h);
        }
    const int y0 = int(std::floor(lo)) - 8;
    const int y1 = int(std::ceil(hi)) + 8 + headroomM;
    return buildTerrainArea(gen, {x0, y0, z0}, {32, y1 - y0, 32});
}

// The area at 1 m: each metre cube takes the material of the voxel at its centre.
VoxelGrid toMetreCubes(const VoxelGrid& g) {
    VoxelGrid m = box(g.dim / kVoxelsPerM);
    for (int x = 0; x < m.dim.x; ++x)
        for (int y = 0; y < m.dim.y; ++y)
            for (int z = 0; z < m.dim.z; ++z)
                m.set({x, y, z}, g.at(glm::ivec3(x, y, z) * kVoxelsPerM + glm::ivec3(kVoxelsPerM / 2)));
    return m;
}

VoxelGrid fromCanvas(const MicroCanvas& c) {
    glm::ivec3 lo, hi;
    VoxelGrid g;
    if (!c.microBounds(lo, hi)) return g;
    g.resize(hi - lo + glm::ivec3(1));
    std::map<std::string, uint8_t> pal;
    for (const auto& v : c.occupiedCells()) {
        const std::string m = c.materialAt(v.x, v.y, v.z);
        auto it = pal.find(m);
        if (it == pal.end()) it = pal.emplace(m, uint8_t(pal.size() + 1)).first;
        g.set(v - lo, it->second);
    }
    return g;
}

StyleProfile cottage() {
    StyleProfileRegistry reg;
    reg.loadFromJson(nlohmann::json::parse(R"({
        "timber_cottage": {
            "roof_style": "gable", "foundation": "crawlspace",
            "thickness": { "exterior_wall": 0.333, "interior_wall": 0.222,
                           "foundation_wall": 0.667, "floor": 0.333, "ceiling": 0.222 },
            "materials": { "structure": "WoodPlanks", "floor": "Wood", "roof": "WoodShingle", "foundation": "Stone" },
            "roof": { "pitch": 0.8 }
        }
    })"));
    return *reg.get("timber_cottage");
}

BuildingProgram twoStory() {
    nlohmann::json j;
    j["name"] = "mesh_case"; j["style"] = "timber_cottage";
    j["footprint"] = nlohmann::json::array({9, 7});
    j["substructure"] = "crawlspace"; j["roof_style"] = "gable";
    j["stories"] = nlohmann::json::array();
    for (int s = 0; s < 2; ++s) {
        nlohmann::json st;
        st["height"] = 3;
        st["rooms"] = nlohmann::json::array({{{"id", "r" + std::to_string(s)},
                                              {"rect", {0, 0, 9, 7}}, {"purpose", "living"}}});
        st["portals"] = nlohmann::json::array();
        if (s == 0)
            st["portals"].push_back({{"between", {"exterior", "r0"}}, {"pos", {4, 0}}, {"width", 1},
                                     {"height", 2}, {"kind", "door"}});
        st["portals"].push_back({{"between", {"exterior", "r" + std::to_string(s)}}, {"pos", {0, 3}},
                                 {"width", 1}, {"height", 1}, {"kind", "window"}});
        st["stairs"] = nlohmann::json::array();
        if (s == 0)
            st["stairs"].push_back({{"from_story", 0}, {"to_story", 1}, {"rect", {6, 1, 2, 5}}, {"form", "straight"}});
        j["stories"].push_back(st);
    }
    return BuildingProgram::fromJson(j);
}

}  // namespace

// ---- 1. exact rigs ---------------------------------------------------------------------------------------

TEST(VoxFoundMesher, SingleVoxelHasSixFaces) {
    VoxelGrid g = box({3, 3, 3});
    g.set({1, 1, 1}, 1);
    EXPECT_EQ(meshCulled(g, Outside::Air).size(), 6u);
    EXPECT_EQ(meshGreedy(g, Outside::Air).size(), 6u);
}

TEST(VoxFoundMesher, SolidBlockMergesToSixQuads) {
    VoxelGrid g = box({8, 8, 8});
    for (size_t i = 0; i < g.size(); ++i) g.mat[i] = 2;
    EXPECT_EQ(meshCulled(g, Outside::Air).size(), 6u * 64u);
    EXPECT_EQ(meshGreedy(g, Outside::Air).size(), 6u);
    EXPECT_EQ(meshGreedy(g, Outside::Air, kBlock).size(), 6u);
    EXPECT_EQ(meshCulled(g, Outside::Skip).size(), 0u) << "a window into a world draws no boundary faces";
}

TEST(VoxFoundMesher, DifferentMaterialsDoNotMergeAndHideTheSharedFace) {
    VoxelGrid g = box({2, 1, 1});
    g.set({0, 0, 0}, 1);
    g.set({1, 0, 0}, 2);
    EXPECT_EQ(meshCulled(g, Outside::Air).size(), 10u);
    EXPECT_EQ(meshGreedy(g, Outside::Air).size(), 10u);
}

// §3.5a point 4: "a flat 32 m wall is a few faces today, but at least 1,024 when merged only per block."
// A 32 x 32 m wall one voxel thick: merged across blocks, 6 quads; per block, 32 x 32 on each big side
// (1,024 each) plus 32 on each of the four edges = 2,176.
TEST(VoxFoundMesher, FlatWallIsSixQuadsAcrossBlocksAndTwoThousandPerBlock) {
    VoxelGrid g = box({256, 256, 1});
    for (size_t i = 0; i < g.size(); ++i) g.mat[i] = 3;
    EXPECT_EQ(meshGreedy(g, Outside::Air).size(), 6u);
    EXPECT_EQ(meshGreedy(g, Outside::Air, kBlock).size(), 2u * 1024u + 4u * 32u);
}

// ---- 2. exactness ----------------------------------------------------------------------------------------

TEST(VoxFoundMesher, MergedMeshCoversEveryExposedFaceExactlyOnce) {
    std::mt19937 rng(11);
    VoxelGrid g = box({24, 20, 28});
    for (size_t i = 0; i < g.size(); ++i) {
        const unsigned r = rng() % 100;
        g.mat[i] = r < 35 ? 1 : r < 45 ? 2 : r < 50 ? 3 : 0;
    }
    // plus a few solid slabs, so long merges happen
    for (int x = 0; x < 24; ++x)
        for (int z = 0; z < 28; ++z) g.set({x, 3, z}, 1);
    for (Outside o : {Outside::Air, Outside::Skip})
        for (int limit : {0, kBlock}) {
            const auto q = meshGreedy(g, o, limit);
            std::string why;
            EXPECT_EQ(coverageErrors(g, o, q, &why), 0u) << "limit " << limit << "\n" << why;
            EXPECT_LT(q.size(), meshCulled(g, o).size());
        }
    std::string why;
    EXPECT_EQ(coverageErrors(g, Outside::Air, meshCulled(g, Outside::Air), &why), 0u) << why;
    // teeth: drop one quad -> errors
    auto q = meshGreedy(g, Outside::Air);
    q.pop_back();
    EXPECT_GT(coverageErrors(g, Outside::Air, q), 0u);
}

TEST(VoxFoundMesher, MergedTerrainMeshIsExact) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    const VoxelArea a = surfaceArea(gen, 0, 0);
    const auto q = meshGreedy(a.grid, Outside::Skip);
    std::string why;
    EXPECT_EQ(coverageErrors(a.grid, Outside::Skip, q, &why), 0u) << why;
}

// ---- 2b. run merge (F2.1, the GPU's form) ------------------------------------------------------------------
// Hand counts (written before the code ran): a single voxel 6; a solid 8^3 block 6 (every face is one full
// 8x8 run stack); two materials side by side 10; the 256 x 256 x 1 wall 6 (each +-z face: 256 identical
// full-width rows -> one quad; each side face: one row).

TEST(VoxFoundMesher, RunMergeHandCounts) {
    VoxelGrid one = box({3, 3, 3});
    one.set({1, 1, 1}, 1);
    EXPECT_EQ(meshRuns(one, Outside::Air).size(), 6u);
    VoxelGrid blk = box({8, 8, 8});
    for (size_t i = 0; i < blk.size(); ++i) blk.mat[i] = 1;
    EXPECT_EQ(meshRuns(blk, Outside::Air).size(), 6u);
    EXPECT_EQ(meshRuns(blk, Outside::Skip).size(), 0u);
    VoxelGrid two = box({2, 1, 1});
    two.set({0, 0, 0}, 1);
    two.set({1, 0, 0}, 2);
    EXPECT_EQ(meshRuns(two, Outside::Air).size(), 10u);
    VoxelGrid wall = box({256, 256, 1});
    for (size_t i = 0; i < wall.size(); ++i) wall.mat[i] = 1;
    EXPECT_EQ(meshRuns(wall, Outside::Air).size(), 6u);
}

TEST(VoxFoundMesher, RunMergeIsExact) {
    std::mt19937 rng(11);
    VoxelGrid g = box({24, 20, 28});
    for (size_t i = 0; i < g.size(); ++i) {
        const unsigned r = rng() % 100;
        g.mat[i] = r < 35 ? 1 : r < 45 ? 2 : r < 50 ? 3 : 0;
    }
    for (int x = 0; x < 24; ++x)
        for (int z = 0; z < 28; ++z) g.set({x, 3, z}, 1);
    for (Outside o : {Outside::Air, Outside::Skip}) {
        const auto q = meshRuns(g, o);
        std::string why;
        EXPECT_EQ(coverageErrors(g, o, q, &why), 0u) << why;
        EXPECT_LT(q.size(), meshCulled(g, o).size());
    }
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    const VoxelArea a = surfaceArea(gen, 0, 0);
    const auto t = meshRuns(a.grid, Outside::Skip);
    std::string why;
    EXPECT_EQ(coverageErrors(a.grid, Outside::Skip, t, &why), 0u) << why;
    std::printf("[mesher] perlin s7 (0,0): runs %zu vs greedy %zu quads\n", t.size(),
                meshGreedy(a.grid, Outside::Skip).size());
}

TEST(VoxFoundMesher, PackedFaceRoundTrips) {
    std::vector<Quad> qs;
    Quad q;
    qs.push_back(q);
    q.o = {255, 255, 255}; q.axis = 2; q.dir = 1; q.du = 256; q.dv = 256; q.material = 255;
    qs.push_back(q);
    q.o = {7, 130, 0}; q.axis = 1; q.dir = -1; q.du = 1; q.dv = 200; q.material = 3;
    qs.push_back(q);
    for (const Quad& in : qs) {
        const Quad out = unpackFace(packFace(in));
        EXPECT_EQ(out.o, in.o);
        EXPECT_EQ(out.axis, in.axis);
        EXPECT_EQ(out.dir, in.dir);
        EXPECT_EQ(out.du, in.du);
        EXPECT_EQ(out.dv, in.dv);
        EXPECT_EQ(out.material, in.material);
    }
    EXPECT_EQ(sizeof(PackedFace), 8u);
}

// ---- F2.3 leaf cards: leaf voxels draw as today's cutout cards, not solid faces ----

TEST(VoxFoundMesher, CardVoxelsEmitNoFacesButStillHideTheirNeighbours) {
    // Log (1) at (1,1,1) with a leaf (2) on its +x side. Predicted with cards = {2}: the log keeps 5 faces
    // (its +x face is hidden by the leaf, as today's occupancy does), the leaf draws none.
    VoxelGrid g = box({4, 3, 3});
    g.set({1, 1, 1}, 1);
    g.set({2, 1, 1}, 2);
    CardSet cards;
    cards.set(2);
    EXPECT_EQ(meshCulled(g, Outside::Air).size(), 10u);   // control: no cards
    const auto culled = meshCulled(g, Outside::Air, &cards);
    EXPECT_EQ(culled.size(), 5u);
    for (const Quad& q : culled) EXPECT_EQ(q.material, 1);
    EXPECT_EQ(meshRuns(g, Outside::Air, &cards).size(), 5u);
    EXPECT_EQ(meshGreedy(g, Outside::Air, 0, &cards).size(), 5u);
    std::string why;
    EXPECT_EQ(coverageErrors(g, Outside::Air, meshRuns(g, Outside::Air, &cards), &why, &cards), 0u) << why;
    // A cards-blind mesh is NOT a valid cards mesh (it draws the leaf).
    EXPECT_GT(coverageErrors(g, Outside::Air, meshRuns(g, Outside::Air), nullptr, &cards), 0u);
}

TEST(VoxFoundMesher, RunMergeWithCardsIsExact) {
    std::mt19937 rng(23);
    VoxelGrid g = box({20, 18, 22});
    for (size_t i = 0; i < g.size(); ++i) {
        const unsigned r = rng() % 100;
        g.mat[i] = r < 30 ? 1 : r < 50 ? 2 : r < 55 ? 3 : 0;
    }
    CardSet cards;
    cards.set(2);
    for (Outside o : {Outside::Air, Outside::Skip}) {
        const auto q = meshRuns(g, o, &cards);
        std::string why;
        EXPECT_EQ(coverageErrors(g, o, q, &why, &cards), 0u) << why;
        for (const Quad& x : q) ASSERT_NE(x.material, 2);
        EXPECT_LT(q.size(), meshRuns(g, o).size());
    }
}

TEST(VoxFoundMesher, SprigHashIsTodaysFoliageHash) {
    // ChunkRenderManager.cpp foliageCellHash01 (today's canopy thinning), reproduced here as the oracle.
    auto today = [](int wx, int wy, int wz) {
        uint32_t h = uint32_t(wx) * 0x8DA6B343u ^ uint32_t(wy) * 0xD8163841u ^ uint32_t(wz) * 0xCB1AB31Fu;
        h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
        return h & 0xFFFFFFu;
    };
    for (glm::ivec3 c : {glm::ivec3{0, 0, 0}, glm::ivec3{1, 2, 3}, glm::ivec3{-5, 700, -123456},
                         glm::ivec3{65535, -1, 31}})
        EXPECT_EQ(sprigHash24(c), today(c.x, c.y, c.z)) << c.x << "," << c.y << "," << c.z;
}

TEST(VoxFoundMesher, SprigThresholdIsDefinedInMetres) {
    // Same sprigs per m^2 at any voxel size: probability = sprigsPerM2 / voxelsPerM^2.
    EXPECT_EQ(sprigThreshold24(2.25, 3), 1u << 22);                      // 0.25 of 2^24 (today's knob)
    EXPECT_EQ(sprigThreshold24(2.25, 8), uint32_t(std::lround(2.25 / 64.0 * 16777216.0)));
    EXPECT_EQ(sprigThreshold24(1e9, 8), 1u << 24);                       // clamps to "keep all"
    EXPECT_EQ(sprigThreshold24(0.0, 8), 0u);
}

TEST(VoxFoundMesher, SprigsAreExposedCardCellsOnly) {
    // 3x3x3 leaf cube centred in a 5^3 grid, keep-all threshold. Prediction: 26 sprigs (every cell but the
    // buried centre), each at its own cell, axis 3, material 2. A log cell (1) emits no sprig.
    VoxelGrid g = box({5, 5, 5});
    for (int x = 1; x <= 3; ++x)
        for (int y = 1; y <= 3; ++y)
            for (int z = 1; z <= 3; ++z) g.set({x, y, z}, 2);
    g.set({0, 0, 0}, 1);
    SprigParams p;
    p.cards.set(2);
    p.threshold24 = 1u << 24;
    const auto s = foliageSprigs(g, Outside::Air, p);
    ASSERT_EQ(s.size(), 26u);
    for (const PackedFace& f : s) {
        EXPECT_TRUE(isSprig(f));
        EXPECT_EQ((f.b >> 16) & 0xFFu, 2u);
        const glm::ivec3 c{int(f.a & 0xFF), int((f.a >> 8) & 0xFF), int((f.a >> 16) & 0xFF)};
        EXPECT_EQ(g.at(c), 2);
        EXPECT_NE(c, glm::ivec3(2, 2, 2));
    }
    EXPECT_EQ(packSprig({1, 2, 3}, 9).a, 1u | (2u << 8) | (3u << 16) | (3u << 24));
    EXPECT_EQ(packSprig({1, 2, 3}, 9).b, 9u << 16);
    // Grid filled with leaves: Air exposes the whole shell, Skip exposes nothing.
    VoxelGrid full = box({3, 3, 3});
    for (auto& m : full.mat) m = 2;
    EXPECT_EQ(foliageSprigs(full, Outside::Air, p).size(), 26u);
    EXPECT_EQ(foliageSprigs(full, Outside::Skip, p).size(), 0u);
    // Zero threshold keeps nothing.
    p.threshold24 = 0;
    EXPECT_EQ(foliageSprigs(g, Outside::Air, p).size(), 0u);
}

TEST(VoxFoundMesher, SprigThinningIsAFunctionOfWorldPosition) {
    // A one-cell-thick leaf slab (every cell exposed via +-y), 64 x 64 cells. Whole grid vs the same slab
    // built as two halves at their own world origins (Outside::Skip): identical world-cell sets, so
    // thinning cannot show a chunk seam. Kept fraction ~ the threshold.
    const glm::ivec3 origin{-40, 300, 1000};
    VoxelGrid whole = box({64, 3, 64});
    for (int x = 0; x < 64; ++x)
        for (int z = 0; z < 64; ++z) whole.set({x, 1, z}, 2);
    SprigParams p;
    p.cards.set(2);
    p.threshold24 = sprigThreshold24(kSprigsPerM2, 8);
    auto worldCells = [](const std::vector<PackedFace>& s, glm::ivec3 o) {
        std::vector<std::tuple<int, int, int>> w;
        for (const PackedFace& f : s)
            w.emplace_back(o.x + int(f.a & 0xFF), o.y + int((f.a >> 8) & 0xFF), o.z + int((f.a >> 16) & 0xFF));
        std::sort(w.begin(), w.end());
        return w;
    };
    p.worldOriginVoxel = origin;
    const auto all = worldCells(foliageSprigs(whole, Outside::Skip, p), origin);
    std::vector<std::tuple<int, int, int>> halves;
    for (int h = 0; h < 2; ++h) {
        VoxelGrid half = box({32, 3, 64});
        for (int x = 0; x < 32; ++x)
            for (int z = 0; z < 64; ++z) half.set({x, 1, z}, 2);
        p.worldOriginVoxel = origin + glm::ivec3(32 * h, 0, 0);
        const auto w = worldCells(foliageSprigs(half, Outside::Skip, p), p.worldOriginVoxel);
        halves.insert(halves.end(), w.begin(), w.end());
    }
    std::sort(halves.begin(), halves.end());
    EXPECT_EQ(all, halves);
    const double kept = double(all.size()) / 4096.0, want = kSprigsPerM2 / 64.0;
    std::printf("[sprigs] slab kept %.4f, threshold %.4f\n", kept, want);
    EXPECT_NEAR(kept, want, 0.35 * want);
    EXPECT_GT(all.size(), 0u);
}

// ---- 3. measured -----------------------------------------------------------------------------------------

TEST(VoxFoundMesher, MeasuredFaceCountsOnGeneratedTerrain) {
    const std::string root = repoRoot();
    ASSERT_FALSE(root.empty()) << "repo root not found from cwd";
    struct Spot { const char* name; WorldGenerator::GenerationType type; uint32_t seed; int x, z; bool flora; };
    const Spot spots[] = {
        {"perlin s7 (0,0)", WorldGenerator::GenerationType::Perlin, 7u, 0, 0, false},
        {"perlin s12345 (-1024,768)", WorldGenerator::GenerationType::Perlin, 12345u, -1024, 768, false},
        {"mountains s7 (2048,1024)", WorldGenerator::GenerationType::Mountains, 7u, 2048, 1024, false},
        {"perlin s7 (0,0) + flora", WorldGenerator::GenerationType::Perlin, 7u, 0, 0, true},
        {"perlin s7 forest (8160,96) + flora", WorldGenerator::GenerationType::Perlin, 7u, 8160, 96, true},
    };
    for (const Spot& s : spots) {
        WorldGenerator gen(s.type, s.seed);
        ASSERT_TRUE(gen.loadBiomes(root + "/resources/biomes.json"));
        VoxelArea a = surfaceArea(gen, s.x, s.z, s.flora ? 40 : 0);
        if (s.flora) stampFlora(a, gen, root + "/resources/templates_g8/nature");
        const size_t culled = meshCulled(a.grid, Outside::Skip).size();
        const size_t greedy = meshGreedy(a.grid, Outside::Skip).size();
        const size_t perBlock = meshGreedy(a.grid, Outside::Skip, kBlock).size();
        const VoxelGrid m = toMetreCubes(a.grid);
        const size_t todayProxy = meshGreedy(m, Outside::Skip, 32).size();
        std::printf("[mesher] %-36s culled %7zu  greedy %6zu  per-block %6zu  | today proxy (1 m cubes) %5zu"
                    "  -> greedy/today %.1fx, per-block/greedy %.2fx\n",
                    s.name, culled, greedy, perBlock, todayProxy,
                    todayProxy ? double(greedy) / todayProxy : 0.0, greedy ? double(perBlock) / greedy : 0.0);
        EXPECT_GT(greedy, 0u) << s.name;
        EXPECT_LE(greedy, perBlock) << s.name;
        EXPECT_LE(perBlock, culled) << s.name;
    }
}

TEST(VoxFoundMesher, MeasuredFaceCountsOnABuildingShellOnBothGrids) {
    const StyleProfile style = cottage();
    const BuildingProgram p = twoStory();
    size_t counts[2] = {0, 0};
    int i = 0;
    for (const StructureGrid g : {StructureGrid::legacy(), StructureGrid::foundation()}) {
        auto sh = StructureRealizer::realizeShell(p, style, g);
        ASSERT_TRUE(sh.ok) << sh.error;
        const VoxelGrid v = fromCanvas(sh.canvas);
        const size_t culled = meshCulled(v, Outside::Air).size();
        const size_t greedy = meshGreedy(v, Outside::Air).size();
        std::string why;
        EXPECT_EQ(coverageErrors(v, Outside::Air, meshGreedy(v, Outside::Air), &why), 0u) << why;
        std::printf("[mesher] shell 9x7 m, 2 stories, grid %d: %zu cells, culled %zu, greedy %zu\n",
                    g.perM, v.solidCount(), culled, greedy);
        counts[i++] = greedy;
    }
    std::printf("[mesher] shell greedy 1/8 m vs 1/9 m: %.2fx\n", double(counts[1]) / double(counts[0]));
}

// ---- F2.7 chunk borders: Neighbours planes (the real voxels across the grid's border) ----

namespace {
using UnitFace = std::tuple<int, int, int, int, int, int>;   // world x, y, z, axis, dir, material

void addUnitFaces(const std::vector<Quad>& quads, glm::ivec3 lo, std::vector<UnitFace>& out) {
    for (const Quad& q : quads) {
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

// Terrain-like world with every border situation: a heightfield of 3 materials, caves through it, leaf (card)
// blobs above, a floating slab, sides not multiples of the chunk size.
VoxelGrid borderWorld() {
    VoxelGrid g = box({50, 40, 37});
    std::mt19937 rng(99);
    for (int x = 0; x < 50; ++x)
        for (int z = 0; z < 37; ++z) {
            const int h = 14 + int(6.0 * std::sin(x * 0.21) + 5.0 * std::cos(z * 0.17));
            for (int y = 0; y < h && y < 40; ++y) {
                const bool cave = std::abs(y - 9) < 2 && ((x / 5 + z / 7) % 3 == 0);
                if (!cave) g.set({x, y, z}, y == h - 1 ? 3 : y < h - 4 ? 1 : 4);
            }
        }
    for (int i = 0; i < 1500; ++i) {
        const glm::ivec3 c(int(rng() % 50), 24 + int(rng() % 14), int(rng() % 37));
        g.set(c, 2);                                                   // leaf voxels
    }
    for (int x = 10; x < 40; ++x)
        for (int z = 12; z < 20; ++z) g.set({x, 33, z}, 1);            // a floating slab across chunk borders
    return g;
}
}  // namespace

TEST(VoxFoundMesher, NeighbourPlanesReproduceBothOutsideModes) {
    VoxelGrid g = box({13, 7, 5});
    std::mt19937 rng(5);
    for (size_t i = 0; i < g.size(); ++i) { const unsigned r = rng() % 100; g.mat[i] = r < 40 ? 1 : r < 55 ? 2 : 0; }
    SprigParams sp;
    sp.cards.set(2);
    sp.threshold24 = 1u << 24;
    for (Outside o : {Outside::Air, Outside::Skip}) {
        Neighbours n;
        n.resize(g.dim, o == Outside::Skip);
        auto k = [](const std::vector<Quad>& qs) {
            std::vector<uint64_t> v;
            for (const Quad& q : qs) v.push_back(packFace(q).key());
            std::sort(v.begin(), v.end());
            return v;
        };
        EXPECT_EQ(k(meshRuns(g, n, &sp.cards)), k(meshRuns(g, o, &sp.cards))) << (o == Outside::Air ? "air" : "skip");
        auto s = [](std::vector<PackedFace> f) {
            std::vector<uint64_t> v;
            for (const PackedFace& p : f) v.push_back(p.key());
            std::sort(v.begin(), v.end());
            return v;
        };
        EXPECT_EQ(s(foliageSprigs(g, n, sp)), s(foliageSprigs(g, o, sp))) << (o == Outside::Air ? "air" : "skip");
    }
}

TEST(VoxFoundMesher, ChunksAreInvisible) {
    // CLAUDE.md "Chunks must not be visible": the region meshed as chunks (each with its Neighbours from the region)
    // has EXACTLY the unit faces and sprigs of the region meshed whole. Merging may differ at chunk borders (a quad
    // cannot cross one); that is cost, not looks, so the comparison is per unit face.
    const VoxelGrid world = borderWorld();
    SprigParams sp;
    sp.cards.set(2);
    sp.worldOriginVoxel = {-77, 300, 5};
    sp.threshold24 = sprigThreshold24(kSprigsPerM2, kVoxelsPerM);
    const glm::ivec3 cs(16, 16, 16);
    for (Outside beyond : {Outside::Air, Outside::Skip}) {
        const char* name = beyond == Outside::Air ? "beyond air" : "beyond skip";
        std::vector<UnitFace> whole, chunked, oldWay;
        addUnitFaces(meshCulled(world, beyond, &sp.cards), {0, 0, 0}, whole);
        std::vector<std::tuple<int, int, int, int>> sprWhole, sprChunked;
        for (const PackedFace& f : foliageSprigs(world, beyond, sp))
            sprWhole.emplace_back(int(f.a & 0xFF), int((f.a >> 8) & 0xFF), int((f.a >> 16) & 0xFF), int(f.b >> 16));
        int chunks = 0;
        for (int x = 0; x < world.dim.x; x += cs.x)
            for (int y = 0; y < world.dim.y; y += cs.y)
                for (int z = 0; z < world.dim.z; z += cs.z) {
                    const glm::ivec3 lo(x, y, z), size = glm::min(cs, world.dim - lo);
                    const VoxelGrid sub = subGrid(world, lo, size);
                    const Neighbours nb = neighboursOf(world, lo, size, beyond);
                    addUnitFaces(meshRuns(sub, nb, &sp.cards), lo, chunked);
                    addUnitFaces(meshRuns(sub, beyond, &sp.cards), lo, oldWay);   // no neighbours: today's F2.1 build
                    SprigParams csp = sp;
                    csp.worldOriginVoxel = sp.worldOriginVoxel + lo;
                    for (const PackedFace& f : foliageSprigs(sub, nb, csp))
                        sprChunked.emplace_back(x + int(f.a & 0xFF), y + int((f.a >> 8) & 0xFF),
                                                z + int((f.a >> 16) & 0xFF), int(f.b >> 16));
                    ++chunks;
                }
        std::sort(whole.begin(), whole.end());
        std::sort(chunked.begin(), chunked.end());
        std::sort(oldWay.begin(), oldWay.end());
        std::sort(sprWhole.begin(), sprWhole.end());
        std::sort(sprChunked.begin(), sprChunked.end());
        EXPECT_EQ(chunks, 4 * 3 * 3) << name;
        EXPECT_GT(whole.size(), 1000u) << name;
        EXPECT_GT(sprWhole.size(), 10u) << name;
        EXPECT_EQ(chunked.size(), whole.size()) << name;
        EXPECT_TRUE(chunked == whole) << name << ": the chunked faces differ from the whole-region faces";
        EXPECT_TRUE(sprChunked == sprWhole) << name << ": chunked sprigs " << sprChunked.size() << " vs whole "
                                            << sprWhole.size();
        // Teeth: without neighbours the chunk borders ARE visible (extra or missing faces at every cut).
        EXPECT_FALSE(oldWay == whole) << name;
        std::printf("[mesher] chunks invisible (%s): whole %zu unit faces; chunks without neighbours %zu; "
                    "sprigs %zu\n", name, whole.size(), oldWay.size(), sprWhole.size());
    }
}

// ---- F2.10 (§4.6 item 4): occlusion from block solidity ---------------------------------------------------------
// Written red against stubs that connect every face and reach every chunk (no culling).
namespace {
VoxelGrid filled(glm::ivec3 d, uint8_t m) {
    VoxelGrid g;
    g.resize(d);
    for (auto& v : g.mat) v = m;
    return g;
}
}  // namespace

TEST(VoxFoundMesher, BlockConnectivityFollowsTodaysRule) {
    const CardSet none;
    // all air: every face sees every face; all solid: nothing
    const FaceConnect air = blockFaceConnectivity(filled({16, 16, 16}, 0), none);
    const FaceConnect rock = blockFaceConnectivity(filled({16, 16, 16}, 1), none);
    for (int f = 0; f < 6; ++f) {
        EXPECT_EQ(air[f], 0x3F) << "face " << f;
        EXPECT_EQ(rock[f], 0) << "face " << f;
    }
    // a one-block-thick solid wall across x in [8,16) of a 24x16x16 grid: -X and +X no longer see each other,
    // every other pair still does (the Y/Z faces span both sides)
    VoxelGrid wall = filled({24, 16, 16}, 0);
    for (int x = 8; x < 16; ++x)
        for (int y = 0; y < 16; ++y)
            for (int z = 0; z < 16; ++z) wall.set({x, y, z}, 1);
    const FaceConnect w = blockFaceConnectivity(wall, none);
    EXPECT_FALSE(facesConnected(w, 0, 1));
    EXPECT_FALSE(facesConnected(w, 1, 0));
    for (int a = 0; a < 6; ++a)
        for (int b = 0; b < 6; ++b)
            if (!((a == 0 && b == 1) || (a == 1 && b == 0)))
                EXPECT_TRUE(facesConnected(w, a, b)) << a << "->" << b;
    // one air voxel on a block corner of the wall: that block passes sight (block-level is conservative)
    VoxelGrid holed = wall;
    holed.set({15, 7, 8}, 0);
    EXPECT_TRUE(facesConnected(blockFaceConnectivity(holed, none), 0, 1));
    // the wall made of a see-through material (leaf cards, glass) does not block
    VoxelGrid leafy = wall;
    for (auto& v : leafy.mat)
        if (v == 1) v = 2;
    CardSet leaves;
    leaves.set(2);
    EXPECT_TRUE(facesConnected(blockFaceConnectivity(leafy, leaves), 0, 1));
    EXPECT_FALSE(facesConnected(blockFaceConnectivity(leafy, none), 0, 1));
}

TEST(VoxFoundMesher, OcclusionWalkHidesWhatIsBehindASolidChunk) {
    const FaceConnect solid{0, 0, 0, 0, 0, 0}, open{0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F};
    const std::vector<glm::ivec3> row{{1, 0, 0}, {2, 0, 0}, {3, 0, 0}};
    auto corridor = [](glm::ivec3 c) { return c.y == 0 && c.z == 0 && c.x >= -1 && c.x <= 4; };
    // camera in (0,0,0), a solid chunk at (1,0,0): it is seen, the two behind it are not
    std::vector<char> r = occlusionReach({{{1, 0, 0}, solid}}, row, {0, 0, 0}, corridor, 1000);
    EXPECT_EQ(r, (std::vector<char>{1, 0, 0}));
    // the same chunk open: all three seen
    r = occlusionReach({{{1, 0, 0}, open}}, row, {0, 0, 0}, corridor, 1000);
    EXPECT_EQ(r, (std::vector<char>{1, 1, 1}));
    // open space above the row: sight goes around the solid chunk and reaches the rest
    auto twoRows = [](glm::ivec3 c) { return c.z == 0 && c.y >= 0 && c.y <= 1 && c.x >= -1 && c.x <= 4; };
    r = occlusionReach({{{1, 0, 0}, solid}}, row, {0, 0, 0}, twoRows, 1000);
    EXPECT_EQ(r, (std::vector<char>{1, 1, 1}));
    // a chunk whose -X face does not lead to its +X face blocks like a solid one
    FaceConnect noThrough = open;
    noThrough[0] &= uint8_t(~(1u << 1));
    noThrough[1] &= uint8_t(~(1u << 0));
    r = occlusionReach({{{1, 0, 0}, noThrough}}, row, {0, 0, 0}, corridor, 1000);
    EXPECT_EQ(r, (std::vector<char>{1, 0, 0}));
    // over budget: keep everything, and say so
    bool over = false;
    r = occlusionReach({{{1, 0, 0}, solid}}, row, {0, 0, 0}, corridor, 1, &over);
    EXPECT_TRUE(over);
    EXPECT_EQ(r, (std::vector<char>{1, 1, 1}));
}

// ---- F2.11 (§4.6 item 5 + item 3's per-block part): face clusters ---------------------------------------------
// Written red against stubs (one cluster with an empty box; every box "in view").

TEST(VoxFoundMesher, FaceBoxMatchesTheDrawnQuad) {
    // a +Y face at (3,4,5) spanning du=2 along z ((1+1)%3) and dv=3 along x: the plane y = 5, z 5..7, x 3..6
    Quad q;
    q.o = {3, 4, 5};
    q.axis = 1;
    q.dir = 1;
    q.du = 2;
    q.dv = 3;
    q.material = 1;
    glm::ivec3 lo, hi;
    faceBox(packFace(q), lo, hi);
    EXPECT_EQ(lo, glm::ivec3(3, 5, 5));
    EXPECT_EQ(hi, glm::ivec3(6, 5, 7));
    // the same face pointing -Y sits on the voxel's own lower plane
    q.dir = -1;
    faceBox(packFace(q), lo, hi);
    EXPECT_EQ(lo, glm::ivec3(3, 4, 5));
    EXPECT_EQ(hi, glm::ivec3(6, 4, 7));
}

TEST(VoxFoundMesher, ClustersKeepEveryFaceAndBoundIt) {
    // generated terrain (engine Perlin s7, 32 x 32 m) meshed with run merge, then clustered at every granularity
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    float gLo = 1e9f, gHi = -1e9f;   // the area spans the actual ground (the first version sat inside solid rock)
    for (int x = -1; x <= 32; ++x)
        for (int z = -1; z <= 32; ++z) {
            gLo = std::min(gLo, gen.sampleSurface(x, z).bedHeight);
            gHi = std::max(gHi, gen.sampleSurface(x, z).bedHeight);
        }
    const int y0 = int(std::floor(gLo)) - 2;
    const VoxelArea a = buildTerrainArea(gen, {0, y0, 0}, {32, int(std::ceil(gHi)) + 3 - y0, 32});
    std::vector<PackedFace> faces;
    for (const Quad& q : meshRuns(a.grid, Outside::Air)) faces.push_back(packFace(q));
    ASSERT_GT(faces.size(), 100u);
    for (int cb : {1, 2, 4, 8, 32}) {
        std::vector<PackedFace> f = faces;
        const std::vector<FaceCluster> cl = clusterFaces(f, cb);
        // same faces (as a multiset), every face in exactly one cluster, clusters contiguous and in order
        std::vector<uint64_t> k0, k1;
        for (const auto& x : faces) k0.push_back(x.key());
        for (const auto& x : f) k1.push_back(x.key());
        std::sort(k0.begin(), k0.end());
        std::sort(k1.begin(), k1.end());
        ASSERT_EQ(k0, k1) << "clusterBlocks " << cb;
        uint32_t next = 0;
        const int cellV = cb * 8;
        for (const FaceCluster& c : cl) {
            ASSERT_EQ(c.first, next) << "clusterBlocks " << cb;
            ASSERT_GT(c.count, 0u);
            glm::ivec3 ulo(std::numeric_limits<int>::max()), uhi(std::numeric_limits<int>::min()), cell(-1);
            for (uint32_t i = c.first; i < c.first + c.count; ++i) {
                glm::ivec3 lo, hi;
                faceBox(f[i], lo, hi);
                ulo = glm::min(ulo, lo);
                uhi = glm::max(uhi, hi);
                const glm::ivec3 o = unpackFace(f[i]).o / cellV;
                if (i == c.first) cell = o;
                ASSERT_EQ(o, cell) << "a cluster mixes cells, clusterBlocks " << cb;
            }
            EXPECT_EQ(c.lo, ulo);
            EXPECT_EQ(c.hi, uhi);
            next += c.count;
        }
        EXPECT_EQ(next, uint32_t(f.size()));
        if (cb == 1) EXPECT_GT(cl.size(), 100u) << "per-block clusters";
        std::printf("[clusters] %zu faces, clusterBlocks %d -> %zu clusters\n", f.size(), cb, cl.size());
    }
}

TEST(VoxFoundMesher, BoxInFrustumIsThePVertexTest) {
    // a box [0,1]^3 against single planes (the other five planes accept everything)
    glm::vec4 p[6];
    for (auto& q : p) q = glm::vec4(0, 0, 0, 1);
    p[0] = glm::vec4(1, 0, 0, -0.5f);   // inside where x >= 0.5: the box straddles -> in
    EXPECT_TRUE(boxInFrustum(p, glm::vec3(0), glm::vec3(1)));
    p[0] = glm::vec4(1, 0, 0, -2.0f);   // inside where x >= 2: the box is fully outside -> out
    EXPECT_FALSE(boxInFrustum(p, glm::vec3(0), glm::vec3(1)));
    p[0] = glm::vec4(-1, 0, 0, 1.0f);   // inside where x <= 1: touching counts as in
    EXPECT_TRUE(boxInFrustum(p, glm::vec3(0), glm::vec3(1)));
    p[0] = glm::vec4(0, 0.6f, 0.8f, -1.5f);   // tilted plane: farthest corner (0,1,1) gives 1.4 - 1.5 < 0 -> out
    EXPECT_FALSE(boxInFrustum(p, glm::vec3(0), glm::vec3(1)));
}

// ---- F2.12 (§4.6 item 6): shadow-caster culling --------------------------------------------------------------------
namespace {
// a cascade's light matrix exactly as RenderCoordinator::fitShadowVolume builds it (camera-relative world -> light clip)
glm::mat4 testLightMatrix(glm::vec3 center, float radius, glm::vec3 lightDir) {
    lightDir = glm::normalize(lightDir);
    const glm::vec3 up = std::abs(lightDir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const float back = 120.0f;
    const glm::mat4 view = glm::lookAt(center - lightDir * (radius + back), center, up);
    glm::mat4 proj = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + 2.0f * back);
    proj[1][1] *= -1;
    return proj * view;
}
}  // namespace

TEST(VoxFoundMesher, LightPlanesMatchTheClipVolume) {
    // a point (a zero-size box) is inside the planes exactly when its clip position is in [-1,1]^2 x [0,1]
    const glm::vec3 center(12.0f, -8.0f, -30.0f), lightDir(0.4f, -0.8f, 0.3f), frameOrigin(-130.5f, -20.0f, 77.25f);
    const glm::mat4 L = testLightMatrix(center, 60.0f, lightDir);
    glm::vec4 planes[6];
    clipPlanesZeroToOne(L, frameOrigin, planes);
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> d(-260.0f, 260.0f);
    size_t in = 0, out = 0, wrong = 0;
    for (int i = 0; i < 20000; ++i) {
        const glm::vec3 p = center - frameOrigin + glm::vec3(d(rng), d(rng), d(rng));   // frame coordinates
        const glm::vec4 c = L * glm::vec4(p + frameOrigin, 1.0f);
        const float margin = std::min({c.w - std::abs(c.x), c.w - std::abs(c.y), c.z, c.w - c.z});
        if (std::abs(margin) < 1e-4f) continue;   // on a plane: either answer
        const bool want = margin > 0.0f;
        (want ? in : out)++;
        if (boxInFrustum(planes, p, p) != want) ++wrong;
    }
    std::printf("[light planes] %zu inside, %zu outside, %zu wrong\n", in, out, wrong);
    EXPECT_GT(in, 500u);
    EXPECT_GT(out, 500u);
    EXPECT_EQ(wrong, 0u);
    for (int k = 0; k < 6; ++k) EXPECT_NEAR(glm::length(glm::vec3(planes[k])), 1.0f, 1e-4f) << "plane " << k;
}

TEST(VoxFoundMesher, CasterBoxFollowsTodaysRuleAndTheLightBox) {
    // mid cascade: fitted sphere radius 60 around the centre, caster reach = 60 + 160 (today's margin)
    const glm::vec3 center(12.0f, -8.0f, -30.0f), lightDir = glm::normalize(glm::vec3(0.4f, -0.8f, 0.3f));
    const glm::vec3 frameOrigin(-130.5f, -20.0f, 77.25f);
    const glm::mat4 L = testLightMatrix(center, 60.0f, lightDir);
    glm::vec4 planes[6];
    clipPlanesZeroToOne(L, frameOrigin, planes);
    const glm::vec3 c = center - frameOrigin;   // the sphere centre in frame coordinates
    const float reach = 60.0f + 160.0f;
    auto kept = [&](glm::vec3 boxCentre) {
        return casterBoxKept(boxCentre - glm::vec3(16.0f), boxCentre + glm::vec3(16.0f), c, reach, planes);
    };
    const glm::vec3 side = glm::normalize(glm::cross(lightDir, glm::vec3(0, 1, 0)));   // across the light
    EXPECT_TRUE(kept(c)) << "a chunk at the centre casts";
    EXPECT_TRUE(kept(c - lightDir * 150.0f)) << "toward the sun, inside the reach and the depth range: casts";
    EXPECT_FALSE(kept(c + glm::vec3(500.0f, 0.0f, 0.0f))) << "beyond the reach: today's distance test drops it";
    EXPECT_FALSE(kept(c + side * 150.0f)) << "inside the reach but beside the light box: the light frustum drops it";
    EXPECT_TRUE(kept(c + side * 70.0f)) << "straddling the light box's side: kept";
}

// ---- F2.13 (§3.5a point 3, §4.6 item 7): the block LOD pyramid ------------------------------------------------------
TEST(VoxFoundMesher, LodLevelIsHalfSolidAtLevelZero) {
    // one 2x2x2 cell: 4 solid (materials 5, 5, 3, 3) -> solid, tie broken to the lower index 3; 3 solid -> air
    VoxelGrid g = box({4, 2, 2});
    g.set({0, 0, 0}, 5);
    g.set({1, 0, 0}, 5);
    g.set({0, 1, 0}, 3);
    g.set({1, 1, 0}, 3);
    g.set({2, 0, 0}, 7);
    g.set({3, 0, 0}, 7);
    g.set({2, 1, 1}, 7);
    const LodGrid l1 = lodDown(lodBase(g));
    ASSERT_EQ(l1.grid.dim, glm::ivec3(2, 1, 1));
    EXPECT_EQ(l1.level, 1);
    EXPECT_EQ(l1.grid.at({0, 0, 0}), 3) << "4 of 8 solid: solid; tie 5 vs 3 -> lowest index";
    EXPECT_EQ(l1.count[l1.grid.idx({0, 0, 0})], 4u);
    EXPECT_EQ(l1.grid.at({1, 0, 0}), 0) << "3 of 8 solid: air";
    EXPECT_EQ(l1.count[l1.grid.idx({1, 0, 0})], 3u);

    // two levels: 5 of the 8 level-1 cells are exactly half solid (solid at level 1), 3 are empty. Level 2 holds
    // 20 of 64 level-0 voxels: AIR, although a majority of majorities would call it solid (that would fatten).
    VoxelGrid h = box({4, 4, 4});
    int filled = 0;
    for (int cx = 0; cx < 2; ++cx)
        for (int cy = 0; cy < 2; ++cy)
            for (int cz = 0; cz < 2; ++cz) {
                if (filled++ >= 5) continue;
                for (int x = 0; x < 2; ++x)
                    for (int z = 0; z < 2; ++z) h.set({cx * 2 + x, cy * 2, cz * 2 + z}, 9);   // bottom half
            }
    const LodGrid h1 = lodDown(lodBase(h));
    size_t solid1 = 0, count1 = 0;
    for (size_t i = 0; i < h1.grid.size(); ++i) {
        solid1 += h1.grid.mat[i] != 0;
        count1 += h1.count[i];
    }
    EXPECT_EQ(solid1, 5u);
    EXPECT_EQ(count1, h.solidCount()) << "counts nest exactly";
    const LodGrid h2 = lodDown(h1);
    ASSERT_EQ(h2.grid.dim, glm::ivec3(1));
    EXPECT_EQ(h2.count[0], 20u);
    EXPECT_EQ(h2.grid.mat[0], 0) << "20 of 64 level-0 voxels: air (no fattening)";
}

TEST(VoxFoundMesher, LodLevelKeepsTheMaterialSeenFromOutside) {
    // a 1 m cell (8^3): one 1/8 m layer of grass (2) on 7 layers of dirt (1), air above. By volume dirt wins 448 to
    // 64, but what the eye sees is the grass: the level-3 voxel must be grass. Measured first on the forest: by volume
    // the 1 m level showed dirt on 39 % of the ground seen from above, 1/8 m on 6 %.
    VoxelGrid g = box({8, 16, 8});
    for (int x = 0; x < 8; ++x)
        for (int z = 0; z < 8; ++z)
            for (int y = 0; y < 8; ++y) g.set({x, y, z}, y == 7 ? 2 : 1);
    LodGrid l = lodBase(g);
    for (int k = 0; k < 3; ++k) l = lodDown(l);
    ASSERT_EQ(l.grid.dim, glm::ivec3(1, 2, 1));
    EXPECT_EQ(l.grid.at({0, 0, 0}), 2) << "the surface material, not the volume majority";
    EXPECT_EQ(l.grid.at({0, 1, 0}), 0) << "the air above stays air";
    // a cell with no exposed voxel (buried) keeps the volume vote
    VoxelGrid b = box({8, 8, 8});
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 8; ++y)
            for (int z = 0; z < 8; ++z) b.set({x, y, z}, y == 7 ? 2 : 1);
    LodGrid lb = lodBase(b);
    for (int k = 0; k < 3; ++k) lb = lodDown(lb);
    EXPECT_EQ(lb.grid.at({0, 0, 0}), 1) << "nothing exposed inside the grid (its border is not air): volume vote";
}

TEST(VoxFoundMesher, LodLevelsAreChunkIndependentAndScaleExactly) {
    const std::string root = repoRoot();
    ASSERT_FALSE(root.empty());
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    ASSERT_TRUE(gen.loadBiomes(root + "/resources/biomes.json"));
    VoxelArea a = surfaceArea(gen, 8160, 96, 40);
    stampFlora(a, gen, root + "/resources/templates_g8/nature");
    // the whole area vs a sub-box aligned to 1 m, built as the engine builds a chunk (with a 1 m margin in x and z,
    // RegionChunk::area): the interior of every level of the sub-box equals the cut of the whole
    const glm::ivec3 lo(64, 0, 96), size(128, a.grid.dim.y, 128), margin(8, 0, 8);
    LodGrid whole = lodBase(a.grid), part = lodBase(subGrid(a.grid, lo - margin, size + 2 * margin));
    for (int k = 1; k <= 3; ++k) {
        whole = lodDown(whole);
        part = lodDown(part);
        const int s = 1 << k;
        const VoxelGrid cut = subGrid(whole.grid, lo / s, size / s);
        const VoxelGrid mine = subGrid(part.grid, margin / s, size / s);
        ASSERT_EQ(cut.dim, mine.dim) << "level " << k;
        size_t diff = 0;
        for (size_t i = 0; i < cut.size(); ++i) diff += cut.mat[i] != mine.mat[i];
        EXPECT_EQ(diff, 0u) << "level " << k << ": the level depends on where the area was cut";
        // faces meshed at level k, scaled to level-0 units: the same boxes x 2^k, on the 2^k grid
        const std::vector<Quad> q = meshRuns(mine, Outside::Skip);
        EXPECT_GT(mine.solidCount(), 0u) << "level " << k << " is empty: nothing compared";
        EXPECT_GT(q.size(), 0u) << "level " << k << " has no faces: nothing compared";
        std::string why;
        EXPECT_EQ(coverageErrors(mine, Outside::Skip, q, &why), 0u) << why;
        size_t bad = 0;
        for (const Quad& c : q) {
            const PackedFace f = lodScaleFace(packFace(c), k);
            glm::ivec3 l0, h0, l1, h1;
            faceBox(packFace(c), l0, h0);
            faceBox(f, l1, h1);
            if (l1 != l0 * s || h1 != h0 * s) ++bad;
        }
        EXPECT_EQ(bad, 0u) << "level " << k << ": scaled faces";
    }
}

TEST(VoxFoundMesher, LodLevelsAboveTheMarginComeFromTheRegionExactly) {
    // F2.14d: levels above 1 m. A chunk's 1 m margin covers its own levels up to 1 m (above); coarser levels need
    // neighbours further away, so they come from the region: every chunk's exact 1 m level is cut to its inner cells
    // and pasted into one region level, then lodDown runs on the region. That must equal the pyramid of the region
    // built whole, every channel (count, surface, both votes, the drawn material), at every level.
    const std::string root = repoRoot();
    ASSERT_FALSE(root.empty());
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    ASSERT_TRUE(gen.loadBiomes(root + "/resources/biomes.json"));
    VoxelArea a = surfaceArea(gen, 8160, 96, 40);
    stampFlora(a, gen, root + "/resources/templates_g8/nature");
    // the region: the area padded with air on top to whole 16 m "chunks" (128 voxels), so level 7 (16 m) nests
    const int kC = 128, kTop = 7;
    const glm::ivec3 dim(a.grid.dim.x, (a.grid.dim.y + kC - 1) / kC * kC, a.grid.dim.z);
    ASSERT_EQ(dim.x % kC, 0);
    ASSERT_EQ(dim.z % kC, 0);
    const VoxelGrid region = subGrid(a.grid, glm::ivec3(0), dim);
    std::vector<LodGrid> whole(kTop + 1);
    whole[0] = lodBase(region);
    for (int k = 1; k <= kTop; ++k) whole[size_t(k)] = lodDown(whole[size_t(k - 1)]);

    // chunk by chunk, as the engine builds (a 1 m margin, clipped where the region ends), then the region pyramid
    const glm::ivec3 chunks = dim / kC, margin(8);
    LodGrid built = lodEmpty(dim / 8, 3);
    for (int cx = 0; cx < chunks.x; ++cx)
        for (int cy = 0; cy < chunks.y; ++cy)
            for (int cz = 0; cz < chunks.z; ++cz) {
                const glm::ivec3 lo = glm::ivec3(cx, cy, cz) * kC;
                const glm::ivec3 alo = glm::max(lo - margin, glm::ivec3(0)),
                                 ahi = glm::min(lo + kC + margin, dim);
                LodGrid part = lodBase(subGrid(region, alo, ahi - alo));
                for (int k = 1; k <= 3; ++k) part = lodDown(part);
                lodPaste(built, lodCut(part, (lo - alo) / 8, glm::ivec3(kC / 8)), lo / 8);
            }
    ASSERT_EQ(built.grid.dim, dim / 8);
    for (int k = 3; k <= kTop; ++k) {
        if (k > 3) built = lodDown(built);
        const LodGrid& w = whole[size_t(k)];
        ASSERT_EQ(built.grid.dim, w.grid.dim) << "level " << k;
        EXPECT_EQ(built.level, k);
        size_t diff[5] = {};
        for (size_t i = 0; i < w.grid.size(); ++i) {
            diff[0] += built.grid.mat[i] != w.grid.mat[i];
            diff[1] += built.count[i] != w.count[i];
            diff[2] += built.surf[i] != w.surf[i];
            diff[3] += built.surfMat[i] != w.surfMat[i];
            diff[4] += built.volMat[i] != w.volMat[i];
        }
        EXPECT_GT(w.grid.solidCount(), 0u) << "level " << k << " is empty: nothing compared";
        EXPECT_EQ(diff[0] + diff[1] + diff[2] + diff[3] + diff[4], 0u)
            << "level " << k << " mat/count/surf/surfMat/volMat differ: " << diff[0] << "/" << diff[1] << "/"
            << diff[2] << "/" << diff[3] << "/" << diff[4];
    }
    // the coarsest level the 3-bit maxLevel holds (7 = 16 m) still fits the record inside a 32 m chunk: a slab of
    // 2 x 2 level-7 cells scales to a 256-voxel face, its +side face onto the chunk's last voxel (255)
    VoxelGrid slab;
    slab.resize({2, 2, 2});
    for (int x = 0; x < 2; ++x)
        for (int z = 0; z < 2; ++z) slab.set({x, 0, z}, 1);
    size_t top = 0;
    for (const Quad& q : meshRuns(slab, Outside::Air)) {
        const Quad s = unpackFace(lodScaleFace(packFace(q), 7));
        if (q.axis == 1 && q.dir > 0) {
            ++top;
            EXPECT_EQ(s.o.y, 127);
            EXPECT_EQ(int(s.du) * int(s.dv), 256 * 256);
        }
        if (q.axis == 0 && q.dir > 0) EXPECT_EQ(s.o.x, 255);
    }
    EXPECT_EQ(top, 1u);
    EXPECT_EQ((lodFlagBits(7, 7, 0) >> 16) & 0xFu, 7u);
    EXPECT_EQ((lodFlagBits(7, 7, 0) >> 20) & 0x7u, 7u);
}

TEST(VoxFoundMesher, LodTopStatsMeasureTheViewFromAbove) {
    // F2.14d look gate for coarse levels. Ground 5 voxels deep (material 1) with one card voxel (material 2) on top and
    // a 3-voxel stone post (material 3) in one column: at level 3 (8-voxel cells) the ground cell is 320/512 solid ->
    // drawn 8 high (error 3/8 cell); the post's column is still one coarse column with the ground's material (the
    // post is 3 of 64 columns: it does not win the cell's surface vote).
    VoxelGrid g;
    g.resize({16, 16, 16});
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 0; y < 5; ++y) g.set({x, y, z}, 1);
    g.set({3, 5, 3}, 2);
    for (int y = 5; y < 8; ++y) g.set({12, y, 12}, 3);
    CardSet cards;
    cards.set(2);
    const LodGrid l0 = lodBase(g);
    LodGrid l3 = l0;
    for (int k = 0; k < 3; ++k) l3 = lodDown(l3);
    const LodTopStats s = lodTopStats(l0, l3, cards);
    // ref mean top per coarse column: 5 everywhere, except the post's column (63 x 5 + 8) / 64
    const double postCol = (63.0 * 5.0 + 8.0) / 64.0;
    EXPECT_NEAR(s.meanErrCells, (3.0 * 3.0 / 8.0 + (8.0 - postCol) / 8.0) / 4.0, 1e-9);
    EXPECT_NEAR(s.maxErrCells, 3.0 / 8.0, 1e-9);
    ASSERT_GE(s.shareRef.size(), 4u);
    ASSERT_GE(s.share.size(), 4u);
    EXPECT_NEAR(s.shareRef[1], 255.0 / 256.0, 1e-9);   // the card voxel does not count; the post's top is stone
    EXPECT_NEAR(s.shareRef[3], 1.0 / 256.0, 1e-9);
    EXPECT_NEAR(s.share[1], 1.0, 1e-9);
    EXPECT_NEAR(s.shareRef[2] + s.share[2], 0.0, 1e-12);
}

TEST(VoxFoundMesher, CoarseLevelsFromTwoPyramidsKeepTheGroundUnderTheCanopy) {
    // F2.14g: dirt (1) 5 voxels deep with a grass (2) top layer, and a canopy of leaves (3, a card material) from
    // y 6 to 13 over half the area -- at level 3 (8-voxel cells) the canopy shares the ground's cell (y 0..7) and fills
    // the cell above. From two pyramids, every column's ground keeps an up face with GRASS on top (the leaves neither
    // hide it nor win its vote); the solid pyramid holds no card material; the leaf pyramid only card material.
    VoxelGrid g;
    g.resize({32, 32, 32});
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z) {
            for (int y = 0; y < 5; ++y) g.set({x, y, z}, 1);
            g.set({x, 5, z}, 2);
            if (x < 16)
                for (int y = 6; y < 14; ++y) g.set({x, y, z}, 3);
        }
    CardSet cards;
    cards.set(3);
    VoxelGrid solid, leaves;
    lodSplitCards(g, cards, solid, leaves);
    ASSERT_EQ(solid.dim, g.dim);
    ASSERT_EQ(leaves.dim, g.dim);
    LodGrid s = lodBase(solid), l = lodBase(leaves);
    for (int k = 0; k < 3; ++k) {
        s = lodDown(s);
        l = lodDown(l);
    }
    size_t cardInSolid = 0, nonCardInLeaves = 0;
    for (uint8_t m : s.grid.mat) cardInSolid += cards.has(m);
    for (uint8_t m : l.grid.mat) nonCardInLeaves += (m != 0 && !cards.has(m));
    EXPECT_EQ(cardInSolid, 0u);
    EXPECT_EQ(nonCardInLeaves, 0u);
    EXPECT_GT(l.grid.solidCount(), 0u) << "the leaf pyramid lost the canopy";
    // up faces on the solid pyramid's mesh (alone, no card set): one per column, at the ground top, grass
    std::map<std::pair<int, int>, std::pair<int, uint8_t>> top;   // column -> (top y, material)
    for (const Quad& q : meshRuns(s.grid, Outside::Skip))
        if (q.axis == 1 && q.dir > 0)
            for (int u = 0; u < q.du; ++u)
                for (int v = 0; v < q.dv; ++v) top[{q.o.x + v, q.o.z + u}] = {q.o.y, q.material};
    EXPECT_EQ(top.size(), size_t(s.grid.dim.x) * size_t(s.grid.dim.z)) << "a column has no top face";
    for (const auto& [col, t] : top) {
        EXPECT_EQ(t.first, 0) << "column " << col.first << "," << col.second << ": top in cell y " << t.first;
        EXPECT_EQ(t.second, 2) << "column " << col.first << "," << col.second << ": top material " << int(t.second);
    }
}

TEST(VoxFoundMesher, LeafMassFillThresholdsTheLeafPyramid) {
    // F2.14h: three level-1 cells (2x2x2 voxels each) of leaves (5, a card material) along x: cell 0 holds 3 leaf
    // voxels (3/8), cell 1 all 8, cell 2 one (1/8). At fill 1/2 only cell 1 is mass; at 1/4 cells 0 and 1; at 1/8 all.
    // Cell 1 is surrounded by leaves on two sides but its outer voxels touch air, so its material comes from the
    // surface vote; a grid that is ALL leaves (no air anywhere inside) has no surface: volume vote.
    VoxelGrid g;
    g.resize({6, 2, 2});
    g.set({0, 0, 0}, 5); g.set({1, 0, 0}, 5); g.set({0, 1, 0}, 5);
    for (int x = 2; x < 4; ++x)
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z) g.set({x, y, z}, 5);
    g.set({4, 0, 0}, 5);
    const LodGrid l1 = lodDown(lodBase(g));
    ASSERT_EQ(l1.grid.dim, glm::ivec3(3, 1, 1));
    EXPECT_EQ(l1.count[0] + l1.count[1] + l1.count[2], 12u);
    struct Case { double fill; uint8_t expect[3]; };
    for (const Case c : {Case{0.5, {0, 5, 0}}, Case{0.25, {5, 5, 0}}, Case{0.125, {5, 5, 5}}}) {
        const VoxelGrid m = lodLeafMass(l1, c.fill, 7);
        ASSERT_EQ(m.dim, l1.grid.dim);
        for (int x = 0; x < 3; ++x)
            EXPECT_EQ(int(m.mat[m.idx({x, 0, 0})]), int(c.expect[x])) << "fill " << c.fill << " cell " << x;
    }
    VoxelGrid full;   // all leaves: nothing touches air inside the grid -> surface vote empty -> volume vote
    full.resize({2, 2, 2});
    for (uint8_t& v : full.mat) v = 6;
    EXPECT_EQ(int(lodLeafMass(lodDown(lodBase(full)), 0.5, 7).mat[0]), 6);
    VoxelGrid sparse;   // one leaf voxel in a 2^3 cell, below half: kept at 1/8 with its surface vote
    sparse.resize({2, 2, 2});
    sparse.set({0, 0, 0}, 6);
    const LodGrid sl = lodDown(lodBase(sparse));
    EXPECT_EQ(int(lodLeafMass(sl, 0.125, 7).mat[0]), sl.surfMat[0] ? int(sl.surfMat[0]) : 7);
}

// F2.15f (section 4.6 item 8's last part): the octant order puts the three directions a camera outside a cell can see
// next to each other for 4 of the 8 octants (runs per cell 1.75 on average vs 2.5 in the axis order), and the merge
// rule (mergeVisibleRuns) joins exactly the contiguous visible groups of one key and multidraw within a block.
TEST(VoxFoundMesher, OctantOrderAndMergeRule) {
    int oneRun = 0, runsOctant = 0, runsAxis = 0;
    for (int oct = 0; oct < 8; ++oct) {
        bool seenO[6] = {}, seenA[6] = {};
        for (int axis = 0; axis < 3; ++axis) {
            const int dir = axis * 2 + ((oct >> axis) & 1);
            seenO[faceDirRank(dir)] = true;
            seenA[dir] = true;
        }
        int ro = 0, ra = 0;
        for (int k = 0; k < 6; ++k) {
            ro += seenO[k] && (k == 0 || !seenO[k - 1]);
            ra += seenA[k] && (k == 0 || !seenA[k - 1]);
        }
        oneRun += ro == 1;
        runsOctant += ro;
        runsAxis += ra;
    }
    EXPECT_EQ(oneRun, 4);
    EXPECT_EQ(runsOctant, 14);   // 1.75 per cell
    EXPECT_EQ(runsAxis, 20);     // 2.5 per cell
    // a cell clustered in the octant order: its direction groups come -x, -y, -z, +x, +y, +z
    std::vector<PackedFace> faces;
    for (int axis = 0; axis < 3; ++axis)
        for (int d = 0; d < 2; ++d) {
            Quad q{};
            q.o = glm::ivec3(1 + axis, 2, 3);
            q.axis = uint8_t(axis);
            q.dir = int8_t(d ? 1 : -1);
            q.du = q.dv = 1;
            q.material = 1;
            faces.push_back(packFace(q));
        }
    std::vector<PackedFace> f2 = faces;
    const std::vector<FaceCluster> groups = splitByDirection(f2, clusterFaces(f2, 4, true));
    ASSERT_EQ(groups.size(), 6u);
    for (int k = 0; k < 6; ++k) EXPECT_EQ(faceDirRank(groups[size_t(k)].dir), k);
    // the merge rule on a hand case: [0,1] join; 2 is culled; 3 starts a new key; 4 has a face gap; 5 crosses the
    // multidraw split (solid = 5); 6, 7 join; 8 is a block boundary (block 8)
    const std::vector<uint32_t> first = {0, 5, 9, 12, 21, 30, 34, 38, 40}, count = {5, 4, 3, 8, 10, 4, 4, 2, 6};
    const std::vector<uint32_t> key = {1, 1, 1, 2, 2, 2, 2, 2, 2};
    const std::vector<uint8_t> vis = {1, 1, 0, 1, 1, 1, 1, 1, 1};
    const std::vector<DrawRun> runs = mergeVisibleRuns(first, count, key, vis, 5, 8);
    const std::vector<std::tuple<uint32_t, uint32_t, uint32_t>> want = {
        {0, 9, 0}, {12, 8, 3}, {21, 10, 4}, {30, 10, 5}, {40, 6, 8}};
    ASSERT_EQ(runs.size(), want.size());
    for (size_t i = 0; i < runs.size(); ++i)
        EXPECT_EQ(std::make_tuple(runs[i].first, runs[i].count, runs[i].cluster), want[i]) << i;
}

TEST(VoxFoundMesher, DirectionGroupsNeverHideAFaceThatFacesTheCamera) {
    // F2.15 (§4.6 item 8): random content, clusters cut into direction groups; for cameras inside and outside the
    // content, every face that faces the camera is in a group the test keeps, and groups wholly behind are dropped.
    std::mt19937 rng(7);
    VoxelGrid g;
    g.resize({48, 40, 56});
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (uint8_t& v : g.mat) v = u(rng) < 0.35f ? uint8_t(1 + (rng() % 3)) : 0;
    std::vector<PackedFace> faces;
    for (const Quad& q : meshRuns(g, Outside::Air)) faces.push_back(packFace(q));
    const size_t total = faces.size();
    const std::vector<FaceCluster> cells = clusterFaces(faces, 2);
    const std::vector<FaceCluster> groups = splitByDirection(faces, cells);
    // the groups partition each cell's range, one direction each, in order
    size_t covered = 0, ci = 0;
    for (const FaceCluster& gr : groups) {
        ASSERT_GE(gr.dir, 0) << "a group with no direction";
        while (ci < cells.size() && gr.first >= cells[ci].first + cells[ci].count) ++ci;
        ASSERT_LT(ci, cells.size());
        EXPECT_GE(gr.first, cells[ci].first);
        EXPECT_LE(gr.first + gr.count, cells[ci].first + cells[ci].count);
        for (uint32_t i = gr.first; i < gr.first + gr.count; ++i) EXPECT_EQ(faceDirIndex(faces[i]), gr.dir);
        covered += gr.count;
    }
    EXPECT_EQ(covered, total);
    EXPECT_GT(groups.size(), cells.size() * 3) << "most cells should hold several directions";
    size_t missed = 0, dropped = 0, checks = 0;
    std::uniform_real_distribution<float> cu(-60.0f, 120.0f);
    for (int t = 0; t < 40; ++t) {
        const glm::vec3 cam(cu(rng), cu(rng), cu(rng));
        for (const FaceCluster& gr : groups) {
            const bool keep = faceGroupMayFaceCamera(gr.dir, glm::vec3(gr.lo), glm::vec3(gr.hi), cam);
            dropped += !keep;
            for (uint32_t i = gr.first; i < gr.first + gr.count; ++i) {
                glm::ivec3 lo, hi;
                faceBox(faces[i], lo, hi);
                const int axis = gr.dir / 2;
                const bool positive = (gr.dir & 1) != 0;
                const bool front = positive ? cam[axis] > float(lo[axis]) : cam[axis] < float(lo[axis]);
                ++checks;
                if (front && !keep) ++missed;
            }
        }
    }
    EXPECT_EQ(missed, 0u) << "a face that faces the camera was culled (" << checks << " checks)";
    EXPECT_GT(dropped, groups.size() * 40 / 4) << "too few groups dropped";
}

TEST(VoxFoundMesher, LightFacingFacesAloneGiveTheSameNearestHit) {
    // F2.15b: random closed content (every exposed face meshed, Outside::Air); rays along random light directions --
    // the nearest hit over the light-facing faces equals the nearest hit over all faces, ray for ray.
    std::mt19937 rng(5);
    VoxelGrid g;
    g.resize({14, 12, 16});
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (uint8_t& v : g.mat) v = u(rng) < 0.3f ? 1 : 0;
    std::vector<Quad> quads = meshRuns(g, Outside::Air);
    ASSERT_GT(quads.size(), 100u);
    auto nearest = [&](glm::vec3 o, glm::vec3 d, bool frontOnly, glm::vec3 toLight) {
        float best = std::numeric_limits<float>::max();
        for (const Quad& q : quads) {
            const int dirIndex = int(q.axis) * 2 + (q.dir > 0 ? 1 : 0);
            if (frontOnly && !faceDirFacesLight(dirIndex, toLight)) continue;
            glm::ivec3 lo, hi;
            faceBox(packFace(q), lo, hi);
            const int a = q.axis;
            if (std::abs(d[a]) < 1e-6f) continue;
            const float t = (float(lo[a]) - o[a]) / d[a];
            if (t <= 0.0f || t >= best) continue;
            const glm::vec3 p = o + t * d;
            const int ua = (a + 1) % 3, va = (a + 2) % 3;
            if (p[ua] >= lo[ua] && p[ua] <= hi[ua] && p[va] >= lo[va] && p[va] <= hi[va]) best = t;
        }
        return best;
    };
    size_t rays = 0, hits = 0, differ = 0;
    std::uniform_real_distribution<float> s(-1.0f, 1.0f);
    for (int l = 0; l < 12; ++l) {
        glm::vec3 toLight(s(rng), std::abs(s(rng)) + 0.2f, s(rng));
        toLight = glm::normalize(toLight);
        for (int r = 0; r < 300; ++r) {
            // start well outside on the light's side, aimed through the grid (irrational-ish offsets avoid edges)
            const glm::vec3 target(0.37f + u(rng) * 13.2f, 0.41f + u(rng) * 11.1f, 0.29f + u(rng) * 15.3f);
            const glm::vec3 o = target + toLight * 60.0f, d = -toLight;
            const float all = nearest(o, d, false, toLight), front = nearest(o, d, true, toLight);
            ++rays;
            hits += all < std::numeric_limits<float>::max();
            differ += std::abs(all - front) > 1e-3f;
        }
    }
    std::printf("[shadow dir] %zu rays, %zu hit, %zu differ\n", rays, hits, differ);
    EXPECT_GT(hits, rays / 2);
    EXPECT_EQ(differ, 0u);
    EXPECT_TRUE(faceDirFacesLight(3, glm::vec3(0, 1, 0)));    // +y faces an overhead sun
    EXPECT_FALSE(faceDirFacesLight(2, glm::vec3(0, 1, 0)));   // -y does not
}

TEST(VoxFoundMesher, CasterSeamAddsExactlyTheLeafHiddenFaces) {
    // F2.15d: random solid + leaf content. Main faces (cards hide) + seam faces (every leaf) = the leaves-as-air mesh
    // as sets of unit faces, with no face in both; at depth 1 the seam holds exactly the hidden faces whose leaf
    // neighbour touches air (inside the grid), and the deeper ones are left out.
    std::mt19937 rng(11);
    VoxelGrid g;
    g.resize({20, 18, 22});
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (uint8_t& v : g.mat) {
        const float r = u(rng);
        v = r < 0.25f ? uint8_t(1 + (rng() % 2)) : r < 0.88f ? uint8_t(6) : uint8_t(0);
    }
    CardSet cards;
    cards.set(6);
    using Key = std::tuple<int, int, int, int, int>;
    auto keys = [](const std::vector<Quad>& qs) {
        std::set<Key> s;
        for (const Quad& q : qs) {
            EXPECT_EQ(q.du * q.dv, 1);
            s.insert({q.o.x, q.o.y, q.o.z, int(q.axis), int(q.dir)});
        }
        return s;
    };
    VoxelGrid solid, leaves;
    lodSplitCards(g, cards, solid, leaves);
    const std::set<Key> main = keys(meshCulled(g, Outside::Air, &cards));
    const std::set<Key> closed = keys(meshCulled(solid, Outside::Air));
    const std::set<Key> seamAll = keys(meshCulled(casterSeamGrid(g, cards, -1, 6), Outside::Skip, &cards));
    std::set<Key> both, uni = main;
    for (const Key& k : seamAll) (main.count(k) ? both : uni).insert(k);
    EXPECT_TRUE(both.empty()) << both.size() << " faces in main and seam";
    EXPECT_EQ(uni, closed) << "main + seam " << uni.size() << " vs leaves-as-air " << closed.size();
    // depth 1: the leaf neighbour of every seam face touches air; every such hidden face is there
    auto leafTouchesAir = [&](glm::ivec3 p) {
        static const glm::ivec3 n6[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (const glm::ivec3& d : n6)
            if (g.in(p + d) && g.mat[g.idx(p + d)] == 0) return true;
        return false;
    };
    std::set<Key> want1;
    for (const Key& k : closed) {
        if (main.count(k)) continue;
        const glm::ivec3 o(std::get<0>(k), std::get<1>(k), std::get<2>(k));
        glm::ivec3 nb = o;
        nb[std::get<3>(k)] += std::get<4>(k);
        if (g.in(nb) && cards.has(g.mat[g.idx(nb)]) && leafTouchesAir(nb)) want1.insert(k);
    }
    const std::set<Key> seam1 = keys(meshCulled(casterSeamGrid(g, cards, 1, 6), Outside::Skip, &cards));
    EXPECT_EQ(seam1, want1);
    EXPECT_GT(want1.size(), 50u);
    EXPECT_LT(want1.size(), seamAll.size()) << "depth 1 should leave some buried faces out";
    std::printf("[caster seam] main %zu, seam all %zu, seam depth 1 %zu\n", main.size(), seamAll.size(), seam1.size());
}

TEST(VoxFoundMesher, ExitFacesCloseTheLightTestNextToLeaves) {
    // F2.15d-2: random solid + leaf content (cards hide their neighbours, so the main mesh is NOT closed). Rays along
    // light directions: the nearest hit over all main faces = over the light-facing main faces + the AWAY side of the
    // exit faces (reach unbounded). Without the exit faces some rays differ (the F2.15b leak). Exit faces are main
    // faces; at reach 2 exactly those of solids within 2 solid steps of a solid with a card neighbour.
    std::mt19937 rng(13);
    VoxelGrid g;
    g.resize({14, 12, 16});
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (uint8_t& v : g.mat) {
        const float r = u(rng);
        v = r < 0.30f ? uint8_t(1 + (rng() % 2)) : r < 0.45f ? uint8_t(6) : uint8_t(0);
    }
    CardSet cards;
    cards.set(6);
    const std::vector<Quad> mainQ = meshRuns(g, Outside::Air, &cards);
    const std::vector<Quad> exitQ = meshRuns(casterExitGrid(g, cards, -1, 6), Outside::Air, &cards);
    ASSERT_GT(exitQ.size(), 20u);
    auto nearest = [](const std::vector<Quad>& qs, glm::vec3 o, glm::vec3 d, glm::vec3 toLight, int mode) {
        // mode 0: every face; 1: light-facing only; 2: away side only
        float best = std::numeric_limits<float>::max();
        for (const Quad& q : qs) {
            const int dirIndex = int(q.axis) * 2 + (q.dir > 0 ? 1 : 0);
            if (mode == 1 && !faceDirFacesLight(dirIndex, toLight)) continue;
            if (mode == 2 && !faceDirFacesLight(dirIndex + kAwayGroup, toLight)) continue;
            glm::ivec3 lo, hi;
            faceBox(packFace(q), lo, hi);
            const int a = q.axis;
            if (std::abs(d[a]) < 1e-6f) continue;
            const float t = (float(lo[a]) - o[a]) / d[a];
            if (t <= 0.0f || t >= best) continue;
            const glm::vec3 p = o + t * d;
            const int ua = (a + 1) % 3, va = (a + 2) % 3;
            if (p[ua] >= lo[ua] && p[ua] <= hi[ua] && p[va] >= lo[va] && p[va] <= hi[va]) best = t;
        }
        return best;
    };
    size_t rays = 0, hits = 0, differ = 0, leakWithout = 0;
    std::uniform_real_distribution<float> s(-1.0f, 1.0f);
    for (int l = 0; l < 12; ++l) {
        const glm::vec3 toLight = glm::normalize(glm::vec3(s(rng), std::abs(s(rng)) + 0.2f, s(rng)));
        for (int r = 0; r < 300; ++r) {
            const glm::vec3 target(0.37f + u(rng) * 13.2f, 0.41f + u(rng) * 11.1f, 0.29f + u(rng) * 15.3f);
            const glm::vec3 o = target + toLight * 60.0f, d = -toLight;
            const float all = nearest(mainQ, o, d, toLight, 0);
            const float front = nearest(mainQ, o, d, toLight, 1);
            const float closed = std::min(front, nearest(exitQ, o, d, toLight, 2));
            ++rays;
            hits += all < std::numeric_limits<float>::max();
            differ += std::abs(all - closed) > 1e-3f;
            leakWithout += std::abs(all - front) > 1e-3f;
        }
    }
    std::printf("[exit faces] %zu rays, %zu hit, %zu differ (light test alone: %zu)\n", rays, hits, differ, leakWithout);
    EXPECT_GT(hits, rays / 2);
    EXPECT_GT(leakWithout, 0u) << "the rig should show the F2.15b leak";
    EXPECT_EQ(differ, 0u);
    EXPECT_TRUE(faceDirFacesLight(kAwayGroup + 2, glm::vec3(0, 1, 0)));    // -y's away side casts under an overhead sun
    EXPECT_FALSE(faceDirFacesLight(kAwayGroup + 3, glm::vec3(0, 1, 0)));   // +y's does not
    // subset + finite reach, as sets of unit faces
    using Key = std::tuple<int, int, int, int, int>;
    auto keys = [](const std::vector<Quad>& qs) {
        std::set<Key> k;
        for (const Quad& q : qs) k.insert({q.o.x, q.o.y, q.o.z, int(q.axis), int(q.dir)});
        return k;
    };
    const std::set<Key> mainU = keys(meshCulled(g, Outside::Air, &cards));
    const std::set<Key> exitAll = keys(meshCulled(casterExitGrid(g, cards, -1, 6), Outside::Air, &cards));
    for (const Key& k : exitAll) EXPECT_TRUE(mainU.count(k)) << "an exit face that is not a main face";
    static const glm::ivec3 n6[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    auto isSolid = [&](glm::ivec3 p) { return g.in(p) && g.mat[g.idx(p)] && !cards.has(g.mat[g.idx(p)]); };
    std::vector<int> dist(g.size(), -1);
    std::vector<glm::ivec3> q;
    for (size_t i = 0; i < g.size(); ++i) {
        const glm::ivec3 p = g.pos(i);
        if (!isSolid(p)) continue;
        for (const glm::ivec3& d : n6)
            if (g.in(p + d) && cards.has(g.mat[g.idx(p + d)])) { dist[i] = 0; q.push_back(p); break; }
    }
    for (size_t h = 0; h < q.size(); ++h)
        for (const glm::ivec3& d : n6) {
            const glm::ivec3 n = q[h] + d;
            if (isSolid(n) && dist[g.idx(n)] < 0) { dist[g.idx(n)] = dist[g.idx(q[h])] + 1; q.push_back(n); }
        }
    std::set<Key> want2;
    for (const Key& k : mainU) {
        const glm::ivec3 o(std::get<0>(k), std::get<1>(k), std::get<2>(k));
        const int dd = dist[g.idx(o)];
        if (dd >= 0 && dd <= 2) want2.insert(k);
    }
    const std::set<Key> exit2 = keys(meshCulled(casterExitGrid(g, cards, 2, 6), Outside::Air, &cards));
    EXPECT_EQ(exit2, want2);
    EXPECT_LT(want2.size(), exitAll.size());
}

TEST(VoxFoundMesher, MeasuredLodLevelsOnAGeneratedForest) {
    // cost inputs for item 7 (not a gate): the §9a forest spot, faces and solid volume per level
    const std::string root = repoRoot();
    ASSERT_FALSE(root.empty());
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    ASSERT_TRUE(gen.loadBiomes(root + "/resources/biomes.json"));
    VoxelArea a = surfaceArea(gen, 8160, 96, 40);
    stampFlora(a, gen, root + "/resources/templates_g8/nature");
    CardSet cards;
    for (size_t m = 1; m < a.palette.size(); ++m)
        if (a.palette[m].rfind("Leaf", 0) == 0) cards.set(uint8_t(m));
    // F2.14d: up to level 7 (16 m): the area padded with air on top to a multiple of 16 m (the levels nest)
    LodGrid l = lodBase(subGrid(a.grid, glm::ivec3(0), {a.grid.dim.x, (a.grid.dim.y + 127) / 128 * 128, a.grid.dim.z}));
    const double vol0 = double(a.grid.solidCount());
    size_t faces0 = 0, prev = 0;
    std::map<std::string, double> share0;   // level 0's share of the up-facing area per material
    LodGrid l3;
    for (int k = 0; k <= 7; ++k) {
        if (k) l = lodDown(l);
        const size_t faces = meshRuns(l.grid, Outside::Skip, &cards).size();
        const double vol = double(l.grid.solidCount()) * double(uint64_t(1) << (3 * k));
        if (!k) faces0 = faces;
        std::printf("[lod] forest (8160,96) level %d (%.3f m): %zu faces (%.3fx L0), solid volume %.4fx L0\n", k,
                    0.125 * (1 << k), faces, double(faces) / double(faces0), vol / vol0);
        // what the eye sees from above: area (m^2) of up-facing (+y) solid faces per material
        std::map<std::string, double> up;
        const double cellM2 = std::pow(0.125 * double(1 << k), 2.0);
        for (const Quad& q : meshRuns(l.grid, Outside::Skip, &cards))
            if (q.axis == 1 && q.dir > 0) up[a.palette[q.material]] += double(q.du) * double(q.dv) * cellM2;
        std::string line;
        for (const auto& [name, m2] : up)
            if (m2 >= 1.0) line += " " + name + " " + std::to_string(int(m2 + 0.5));
        std::printf("[lod]   up-facing m^2:%s\n", line.c_str());
        // gate (the look from above) for the levels inside a block (to 1 m): every material holding >= 5 % of level
        // 0's up-facing area keeps its share within 10 percentage points
        double total = 0.0;
        for (const auto& [name, m2] : up) total += m2;
        for (const auto& [name, m2] : up) {
            if (!k) share0[name] = m2 / total;
        }
        for (const auto& [name, s0] : share0) {
            if (s0 < 0.05 || k > 3) continue;
            const double sk = up.count(name) ? up[name] / total : 0.0;
            EXPECT_NEAR(sk, s0, 0.10) << "level " << k << ": " << name << " share of the ground seen from above";
        }
        // F2.14d, above 1 m: this 32 m area has only 8x8 columns at 4 m and 2x2 at 16 m (a column's ground moves by
        // up to half a cell, thin trunks vanish), so the share is not a gate here (it is measured on the whole gate
        // region, lod_top_view), nor is the top seen from above vs level 3: an assert of mean <= 0.5 cell was tried
        // here and failed at 4 / 8 m (0.53 / 0.56, max 4.75 cells = vanished trunk tops; VoxelFoundationPlan F2.14d)
        if (k == 3) l3 = l;
        if (k > 3) {
            const LodTopStats ts = lodTopStats(l3, l, cards);
            std::printf("[lod]   top vs 1 m: mean %.3f cells, max %.3f\n", ts.meanErrCells, ts.maxErrCells);
        }
        if (k) EXPECT_LT(faces, prev) << "level " << k;
        prev = faces;
    }
}

// ---- F2.14 (§3.5a point 3, §4.6 item 7): the level chosen by size on screen, dithered between levels ----------------
TEST(VoxFoundMesher, LodLevelIsSizeOnScreen) {
    const float f = 870.0f;
    for (float P : {1.0f, 2.0f, 4.0f}) {
        const float c = std::log2(8.0f * P / f);
        EXPECT_NEAR(lodScaleFromCode(lodScaleCode(P, f)), c, 1.0f / 16.0f + 1e-5f) << P;
        for (int k = 0; k <= 3; ++k) {
            // level k's voxel (2^k / 8 m) covers exactly P pixels at this distance
            const float d = (float(1 << k) / 8.0f) * f / P;
            EXPECT_NEAR(lodLevelAt(d, c), float(k), 1e-4f) << "P " << P << " level " << k;
            EXPECT_NEAR(lodLevelAt(2.0f * d, c), float(k) + 1.0f, 1e-4f);   // twice as far: one level coarser
        }
    }
    EXPECT_EQ(lodScaleCode(1e9f, 1.0f), 255);   // clamped to the 8 bits
    EXPECT_EQ(lodScaleCode(1e-9f, 1.0f), 0);
}

TEST(VoxFoundMesher, LodPickRampsAcrossTheBandWithoutAPop) {
    std::vector<float> us;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) us.push_back(lodBayer(x, y));
    std::vector<float> sorted = us;
    std::sort(sorted.begin(), sorted.end());
    for (int i = 0; i < 16; ++i) EXPECT_FLOAT_EQ(sorted[size_t(i)], (float(i) + 0.5f) / 16.0f);   // a permutation
    EXPECT_FLOAT_EQ(lodBayer(5, 6), lodBayer(1, 2));                                               // tiles every 4 px
    EXPECT_EQ(lodPickLevel(-5.0f, 0.5f, 3), 0);
    EXPECT_EQ(lodPickLevel(10.0f, 0.5f, 3), 3);
    const float dL = 1.0f / 64.0f;
    for (int k = 1; k <= 3; ++k) {
        float prev = -1.0f;
        // half a level either side of switch k (further down, switch k-1's band is in play)
        for (float L = float(k) - 0.5f; L <= float(k) + 0.5f; L += dL) {
            int coarser = 0;
            for (float u : us) {
                const int p = lodPickLevel(L, u, 3);
                ASSERT_TRUE(p == k - 1 || p == k) << "L " << L;   // only the two levels meeting here
                coarser += p == k ? 1 : 0;
            }
            const float share = float(coarser) / 16.0f;
            if (L <= float(k) - kLodBand * 0.5f) EXPECT_EQ(share, 0.0f) << "L " << L;
            if (L >= float(k) + kLodBand * 0.5f) EXPECT_EQ(share, 1.0f) << "L " << L;
            if (prev >= 0.0f) {
                EXPECT_GE(share, prev) << "L " << L;                                          // monotone
                EXPECT_LE(share - prev, dL / kLodBand + 1.0f / 16.0f + 1e-6f) << "L " << L;   // no pop
            }
            prev = share;
        }
    }
}

TEST(VoxFoundMesher, LodLevelsNeverLeaveAHoleWhereTheyMeet) {
    std::vector<float> us;
    for (int i = 0; i < 16; ++i) us.push_back(lodBayer(i % 4, i / 4));
    // Without slack the levels partition the pixels exactly: at one L, exactly one level keeps.
    for (float L = -1.0f; L <= 4.5f; L += 1.0f / 128.0f)
        for (float u : us) {
            int kept = 0;
            for (int k = 0; k <= 3; ++k) kept += lodKeeps(k, L, u, 3, 0.0f) ? 1 : 0;
            ASSERT_EQ(kept, 1) << "L " << L << " u " << u;
        }
    EXPECT_TRUE(lodKeeps(2, 2.0f, 0.5f, 3));
    EXPECT_FALSE(lodKeeps(0, 2.0f, 0.5f, 3));
    // Different levels' surfaces at one pixel lie at slightly different distances. With the slack, any surfaces whose
    // L lie within kLodSlack of each other leave no pixel where every level discards; at most 2 levels keep at one L.
    std::mt19937 rng(14);
    std::uniform_real_distribution<float> base(-1.0f, 4.0f), off(0.0f, kLodSlack);
    auto holes = [&](float slack) {
        int h = 0;
        for (int t = 0; t < 20000; ++t) {
            const float b = base(rng);
            float Lj[4];
            for (float& l : Lj) l = b + off(rng);
            for (float u : us) {
                bool any = false;
                for (int k = 0; k <= 3; ++k) any = any || lodKeeps(k, Lj[k], u, 3, slack);
                h += any ? 0 : 1;
            }
        }
        return h;
    };
    EXPECT_EQ(holes(kLodSlack), 0);
    EXPECT_GT(holes(0.0f), 0) << "sensitivity: without the slack such surfaces DO leave holes";
    for (float L = -1.0f; L <= 4.5f; L += 1.0f / 128.0f)
        for (float u : us) {
            int kept = 0;
            for (int k = 0; k <= 3; ++k) kept += lodKeeps(k, L, u, 3) ? 1 : 0;
            ASSERT_GE(kept, 1);
            ASSERT_LE(kept, 2) << "overdraw stays bounded";
        }
}

TEST(VoxFoundMesher, LodBoxRangeCoversEveryFragmentInIt) {
    std::mt19937 rng(214);
    std::uniform_real_distribution<float> pos(-200.0f, 200.0f), size(0.5f, 32.0f), t01(0.0f, 1.0f);
    const float f = 870.0f;
    int widened = 0;
    for (int t = 0; t < 3000; ++t) {
        const glm::vec3 lo(pos(rng), pos(rng) * 0.2f, pos(rng));
        const glm::vec3 hi = lo + glm::vec3(size(rng), size(rng), size(rng));
        const glm::vec3 cam(pos(rng), pos(rng) * 0.3f, pos(rng));
        const float P = float(1 << (t % 4));
        const float c = std::log2(8.0f * P / f);
        const glm::ivec2 r = lodLevelsForBox(lo, hi, cam, c, 3);
        ASSERT_LE(r.x, r.y);
        widened += r.y > r.x ? 1 : 0;
        for (int s = 0; s < 40; ++s) {
            const glm::vec3 p = lo + (hi - lo) * glm::vec3(s < 8 ? float(s & 1) : t01(rng),
                                                           s < 8 ? float((s >> 1) & 1) : t01(rng),
                                                           s < 8 ? float((s >> 2) & 1) : t01(rng));
            const float L = lodLevelAt(glm::length(p - cam), c);
            for (int i = 0; i < 16; ++i)
                for (int k = 0; k <= 3; ++k)
                    if (lodKeeps(k, L, lodBayer(i % 4, i / 4), 3))
                        ASSERT_TRUE(k >= r.x && k <= r.y) << "a kept fragment's level is not drawn for its box";
        }
    }
    EXPECT_GT(widened, 0);
    // a small box far away draws one level (here L ~ 2.50: mid-way between switches); the camera inside a box draws
    // from level 0
    EXPECT_EQ(lodLevelsForBox(glm::vec3(355.0f), glm::vec3(356.0f), glm::vec3(0.0f), std::log2(8.0f / f), 3),
              glm::ivec2(2, 2));
    EXPECT_EQ(lodLevelsForBox(glm::vec3(-1.0f), glm::vec3(1.0f), glm::vec3(0.0f), std::log2(8.0f / f), 3).x, 0);
}

// F2.14 casters: the light's view floors the level at the voxel one shadow texel wide; the box range must still cover
// every caster fragment (which keeps level k when lodKeeps(k, max(L, floor), u)).
TEST(VoxFoundMesher, LodCasterFloorIsTheShadowTexelAndTheBoxCoversIt) {
    EXPECT_NEAR(lodTexelFloor(0.125f), 0.0f, 1e-5f);    // a 1/8 m texel: level 0
    EXPECT_NEAR(lodTexelFloor(0.5f), 2.0f, 1e-5f);      // a 1/2 m texel: level 2
    EXPECT_LT(lodTexelFloor(0.104f), 0.0f);             // today's mid cascade (8192 over ~850 m): inert
    std::mt19937 rng(2142);
    std::uniform_real_distribution<float> pos(-200.0f, 200.0f), size(0.5f, 32.0f), t01(0.0f, 1.0f), fl(-1.0f, 3.5f);
    const float f = 1086.0f;
    int raised = 0;
    for (int t = 0; t < 3000; ++t) {
        const glm::vec3 lo(pos(rng), pos(rng) * 0.2f, pos(rng));
        const glm::vec3 hi = lo + glm::vec3(size(rng), size(rng), size(rng));
        const glm::vec3 cam(pos(rng), pos(rng) * 0.3f, pos(rng));
        const float c = std::log2(8.0f * float(1 << (t % 4)) / f);
        const float floorL = fl(rng);
        const glm::ivec2 r = lodLevelsForBox(lo, hi, cam, c, 3, floorL);
        const glm::ivec2 r0 = lodLevelsForBox(lo, hi, cam, c, 3);
        ASSERT_LE(r.x, r.y);
        ASSERT_GE(r.x, r0.x);   // a floor never makes a box finer
        raised += r.x > r0.x ? 1 : 0;
        for (int s = 0; s < 40; ++s) {
            const glm::vec3 p = lo + (hi - lo) * glm::vec3(t01(rng), t01(rng), t01(rng));
            const float L = std::max(lodLevelAt(glm::length(p - cam), c), floorL);
            for (int i = 0; i < 16; ++i)
                for (int k = 0; k <= 3; ++k)
                    if (lodKeeps(k, L, lodBayer(i % 4, i / 4), 3))
                        ASSERT_TRUE(k >= r.x && k <= r.y) << "a kept caster fragment's level is not drawn for its box";
        }
    }
    EXPECT_GT(raised, 300);   // the floor does change boxes
    // the camera inside the box with a floor of 2.5 draws level 2 and up only, never 1/8 m
    EXPECT_EQ(lodLevelsForBox(glm::vec3(-1.0f), glm::vec3(1.0f), glm::vec3(0.0f), std::log2(8.0f / f), 3, 2.5f).x, 2);
}

// F2.14 casters: at any distance, no caster texel (any Bayer value) may keep a level COARSER than the finest level a
// screen pixel (any Bayer value) may show there -- else the coarse caster shadows the fine surface (the stipple). And
// casters must still go coarse: at every L, some caster level >= the main view's finest minus 1.
TEST(VoxFoundMesher, LodCasterIsNeverCoarserThanTheSurfaceItShadows) {
    for (int floorCase = 0; floorCase < 2; ++floorCase)
        for (float L = -1.0f; L <= 5.0f; L += 0.005f) {
            const float floorL = floorCase ? -0.15f : -1e9f;   // today's mid texel floor (inert)
            int viewFinest = 99, casterCoarsest = -1, casterFinest = 99;
            for (int i = 0; i < 16; ++i) {
                const float u = lodBayer(i % 4, i / 4);
                for (int k = 0; k <= 3; ++k) {
                    if (lodKeeps(k, L, u, 3)) viewFinest = std::min(viewFinest, k);
                    if (lodCasterKeeps(k, L, u, 3, floorL)) {
                        casterCoarsest = std::max(casterCoarsest, k);
                        casterFinest = std::min(casterFinest, k);
                    }
                }
            }
            ASSERT_LE(casterCoarsest, viewFinest) << "L=" << L << ": a caster is coarser than a surface it shadows";
            ASSERT_GE(casterFinest, viewFinest - 1) << "L=" << L << ": casters lag more than one level";
            ASSERT_GE(casterCoarsest, 0) << "L=" << L << ": a texel keeps no caster level (a hole in the shadow)";
        }
    // each texel keeps at least one caster level (no hole), at every L and u
    for (float L = -1.0f; L <= 5.0f; L += 0.01f)
        for (int i = 0; i < 16; ++i) {
            int n = 0;
            for (int k = 0; k <= 3; ++k) n += lodCasterKeeps(k, L, lodBayer(i % 4, i / 4), 3, -1e9f) ? 1 : 0;
            ASSERT_GE(n, 1);
        }
}

// F2.14c: sprigs (levels < h) hand off to coarse leaf mass (levels >= h). The sprig dither runs per SPRIG-covered pixel
// with its own L (the card centre's distance), the mass per pixel: wherever the canopy is, at least one of them is kept
// for every pair of Bayer values (no hole), even with the two L a little apart; the fade threshold is the same test;
// sprigs are gone a little past the band and mass is absent before it.
TEST(VoxFoundMesher, LodSprigsHandOffToLeafMassWithoutAHole) {
    for (int h = 1; h <= 3; ++h)
        for (float L = -1.0f; L <= 5.0f; L += 0.003f) {
            for (int i = 0; i < 16; ++i) {
                const float u = lodBayer(i % 4, i / 4);
                const bool kept = lodSprigKeeps(L, u, h);
                ASSERT_EQ(kept, u < lodSprigFade(L, h)) << "fade threshold != keep test at L=" << L;
                ASSERT_EQ(kept, lodPickLevel(L - kLodSlack, u, 3) < h);
            }
            for (float dL : {0.0f, 0.03f, -0.03f})   // sprig L vs mass L (card centre vs the mass fragment)
                for (int i = 0; i < 16; ++i)
                    for (int j = 0; j < 16; ++j) {
                        const float us = lodBayer(i % 4, i / 4), up = lodBayer(j % 4, j / 4);
                        bool mass = false;
                        for (int k = h; k <= 3; ++k) mass = mass || lodKeeps(k, L + dL, up, 3);
                        ASSERT_TRUE(lodSprigKeeps(L, us, h) || mass) << "canopy hole at L=" << L << " h=" << h;
                    }
            if (L >= float(h) + 0.75f) ASSERT_LE(lodSprigFade(L, h), 0.0f) << "sprigs drawn past the band";
            if (L <= float(h) - 0.75f) {
                for (int i = 0; i < 16; ++i)
                    for (int k = h; k <= 3; ++k) ASSERT_FALSE(lodKeeps(k, L, lodBayer(i % 4, i / 4), 3));
                ASSERT_GE(lodSprigFade(L, h), 1.0f) << "sprigs thinned before the band";
            }
        }
}

TEST(VoxFoundMesher, LodFlagBitsLeaveTheStaticFlagsAlone) {
    for (int k = 0; k <= 7; ++k)
        for (int code : {0, 77, 255}) {
            const uint32_t b = lodFlagBits(k, 7 - k % 8, code);
            EXPECT_EQ(b & 0xFFFFu, 0u);   // static faces' flags live in bits 0-15
            EXPECT_EQ((b >> 16) & 0xFu, uint32_t(k));
            EXPECT_EQ((b >> 20) & 0x7u, uint32_t(7 - k % 8));
            EXPECT_EQ((b >> 23) & 1u, 1u);
            EXPECT_EQ(b >> 24, uint32_t(code));
        }
}

// ---- F2.16 (§4.6 item 9): incremental rebuild ------------------------------------------------------------------
namespace {
std::vector<uint64_t> sortedKeys(const std::vector<PackedFace>& v, size_t first = 0,
                                 size_t count = std::numeric_limits<size_t>::max()) {
    std::vector<uint64_t> k;
    for (size_t i = first; i < v.size() && i - first < count; ++i) k.push_back(v[i].key());
    std::sort(k.begin(), k.end());
    return k;
}
std::vector<PackedFace> packedRuns(const VoxelGrid& g, Edges e, const CardSet* cards) {
    std::vector<PackedFace> out;
    for (const Quad& q : meshRuns(g, e, cards)) out.push_back(packFace(q));
    return out;
}
}  // namespace

TEST(VoxFoundMesher, IncrementalRebuildEqualsAFullRebuild) {
    // Random terrain-like grids (awkward sizes, real border planes or Outside::Air), material 4 a card (leaf), and 300
    // random edits: boxes and spheres from 1 voxel to a third of the grid, edits on the grid border and edits of a
    // border plane alone. After EVERY edit the incremental state must equal a full rebuild of the new voxels: faces
    // (as a set, and exact coverage), the direction groups (same groups, boxes and face sets), the cells, the sprigs,
    // and -- where the sides are multiples of 8 -- the block connectivity.
    std::mt19937 rng(9216u);
    auto rnd = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    struct Case { glm::ivec3 dim; bool planes; };
    const Case cases[] = {{{40, 24, 48}, true}, {{33, 17, 1}, false}, {{1, 9, 33}, true}, {{24, 40, 16}, false}};
    CardSet cards;
    cards.set(4);
    size_t rowsMeshed = 0, rowsFull = 0, edits = 0;
    for (const Case& cs : cases) {
        const glm::ivec3 d = cs.dim;
        VoxelGrid g;
        g.resize(d);
        for (int x = 0; x < d.x; ++x)
            for (int z = 0; z < d.z; ++z) {
                const int h = d.y / 3 + rnd(0, std::max(1, d.y / 4));
                for (int y = 0; y < d.y; ++y) {
                    uint8_t m = 0;
                    if (y < h - 3) m = 1;
                    else if (y < h - 1) m = 2;
                    else if (y < h) m = 3;
                    else if (rnd(0, 99) < 6) m = uint8_t(rnd(1, 4));
                    g.set({x, y, z}, m);
                }
            }
        for (int b = 0; b < 3; ++b) {   // leaf blobs
            const glm::ivec3 c(rnd(0, d.x - 1), rnd(0, d.y - 1), rnd(0, d.z - 1));
            const int r = rnd(1, 4);
            for (int x = c.x - r; x <= c.x + r; ++x)
                for (int y = c.y - r; y <= c.y + r; ++y)
                    for (int z = c.z - r; z <= c.z + r; ++z)
                        if (g.in({x, y, z}) && glm::length(glm::vec3(x, y, z) - glm::vec3(c)) <= float(r))
                            g.set({x, y, z}, 4);
        }
        Neighbours nb;
        nb.resize(d, false);
        for (int a = 0; a < 3; ++a)
            for (int s : {-1, 1})
                for (int v = 0; v < d[(a + 2) % 3]; ++v)
                    for (int u = 0; u < d[(a + 1) % 3]; ++u)
                        if (rnd(0, 1)) nb.set(a, s, u, v, true);
        const Edges e = cs.planes ? Edges(nb) : Edges(Outside::Air);
        SprigParams sp;
        sp.cards = cards;
        sp.worldOriginVoxel = {1003, -37, 211};
        sp.threshold24 = 1u << 23;
        const bool conn = d.x % 8 == 0 && d.y % 8 == 0 && d.z % 8 == 0;

        std::vector<PackedFace> faces = packedRuns(g, e, &cards);
        std::vector<FaceCluster> groups = splitByDirection(faces, clusterFaces(faces, 1));
        std::vector<PackedFace> sprigs = foliageSprigs(g, e, sp);
        std::vector<uint8_t> blocking = conn ? blockBlocking(g, cards) : std::vector<uint8_t>();

        for (int it = 0; it < 75; ++it, ++edits) {
            const int kind = rnd(0, 9);   // 0-6 box, 7-8 sphere, 9 border plane only
            const uint8_t m = rnd(0, 99) < 35 ? 0 : uint8_t(rnd(1, 4));
            glm::ivec3 lo, hi;
            if (kind == 9 && cs.planes) {
                const int a = rnd(0, 2), s = rnd(0, 1) ? 1 : -1, ua = (a + 1) % 3, va = (a + 2) % 3;
                const int u0 = rnd(0, d[ua] - 1), v0 = rnd(0, d[va] - 1);
                const int u1 = std::min(d[ua] - 1, u0 + rnd(0, d[ua] / 3));
                const int v1 = std::min(d[va] - 1, v0 + rnd(0, d[va] / 3));
                const bool solid = rnd(0, 1) != 0;
                for (int v = v0; v <= v1; ++v)
                    for (int u = u0; u <= u1; ++u) nb.set(a, s, u, v, solid);
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
                            if (glm::length(glm::vec3(x, y, z) - glm::vec3(c)) <= float(r) + 0.5f) g.set({x, y, z}, m);
            } else {
                for (int a = 0; a < 3; ++a) {
                    lo[a] = rnd(-1, d[a]);
                    hi[a] = std::min(d[a], lo[a] + rnd(0, std::max(1, d[a] / 3)));
                }
                for (int x = std::max(lo.x, 0); x <= std::min(hi.x, d.x - 1); ++x)
                    for (int y = std::max(lo.y, 0); y <= std::min(hi.y, d.y - 1); ++y)
                        for (int z = std::max(lo.z, 0); z <= std::min(hi.z, d.z - 1); ++z) g.set({x, y, z}, m);
            }
            const RunsDelta delta = meshRunsDelta(g, e, faces, lo, hi, &cards);
            rowsMeshed += delta.rowsMeshed;
            rowsFull += size_t(2) * (size_t(d.x) * d.y + size_t(d.y) * d.z + size_t(d.z) * d.x);
            applyClusteredDelta(faces, groups, delta, 1);
            updateSprigs(sprigs, g, e, sp, lo, hi);
            if (conn) updateBlockBlocking(g, cards, lo, hi, blocking);

            std::vector<PackedFace> want = packedRuns(g, e, &cards);
            const std::vector<FaceCluster> wantCells = clusterFaces(want, 1);
            const std::vector<FaceCluster> wantGroups = splitByDirection(want, wantCells);
            const std::string at = "case " + std::to_string(d.x) + "x" + std::to_string(d.y) + "x" +
                                   std::to_string(d.z) + " edit " + std::to_string(it) + " kind " + std::to_string(kind);
            ASSERT_EQ(sortedKeys(faces), sortedKeys(want)) << at;
            if (it % 15 == 0) {
                std::vector<Quad> qs;
                for (const PackedFace& f : faces) qs.push_back(unpackFace(f));
                std::string why;
                ASSERT_EQ(coverageErrors(g, e, qs, &why, &cards), 0u) << at << "\n" << why;
            }
            ASSERT_EQ(groups.size(), wantGroups.size()) << at;
            for (size_t i = 0; i < groups.size(); ++i) {
                ASSERT_EQ(groups[i].first, wantGroups[i].first) << at << " group " << i;
                ASSERT_EQ(groups[i].count, wantGroups[i].count) << at << " group " << i;
                ASSERT_EQ(groups[i].dir, wantGroups[i].dir) << at << " group " << i;
                ASSERT_EQ(groups[i].lo, wantGroups[i].lo) << at << " group " << i;
                ASSERT_EQ(groups[i].hi, wantGroups[i].hi) << at << " group " << i;
                ASSERT_EQ(sortedKeys(faces, groups[i].first, groups[i].count),
                          sortedKeys(want, wantGroups[i].first, wantGroups[i].count))
                    << at << " group " << i;
            }
            const std::vector<FaceCluster> cells = cellsOfGroups(faces, groups, 1);
            ASSERT_EQ(cells.size(), wantCells.size()) << at;
            for (size_t i = 0; i < cells.size(); ++i) {
                ASSERT_EQ(cells[i].first, wantCells[i].first) << at << " cell " << i;
                ASSERT_EQ(cells[i].count, wantCells[i].count) << at << " cell " << i;
                ASSERT_EQ(cells[i].lo, wantCells[i].lo) << at << " cell " << i;
                ASSERT_EQ(cells[i].hi, wantCells[i].hi) << at << " cell " << i;
            }
            ASSERT_EQ(sortedKeys(sprigs), sortedKeys(foliageSprigs(g, e, sp))) << at;
            if (conn) {
                ASSERT_EQ(blocking, blockBlocking(g, cards)) << at;
                ASSERT_EQ(connectivityFromBlocking(d / 8, blocking), blockFaceConnectivity(g, cards)) << at;
            }
        }
    }
    EXPECT_EQ(edits, 300u);
    std::printf("[incremental] %zu edits: %zu rows meshed of the %zu a full rebuild per edit meshes (%.2f %%)\n", edits,
                rowsMeshed, rowsFull, 100.0 * double(rowsMeshed) / double(rowsFull));
}

// F2.17 (§4.6 item 10): the face pool. Synthetic chunks whose face / cluster counts change at random (shrink, grow past
// the slack, to 0 and back). After every step: each chunk's drawn range and cluster slots are its lists (firstFace
// remapped, count-0 slots after them), spans never overlap, unchanged chunks never move, every live cluster points
// inside its own chunk's drawn faces, and the PREVIOUS image with only the dirty ranges copied from the new one equals
// the new one -- the renderer's partial upload.
TEST(VoxFoundMesher, FacePoolUpdatesOnlyWhatChanged) {
    std::mt19937 rng(217);
    const size_t nChunks = 6;
    std::vector<std::vector<PackedFace>> faces(nChunks);
    std::vector<PoolChunk> in(nChunks);
    uint32_t stamp = 0;
    auto remake = [&](size_t i, uint32_t n) {
        faces[i].resize(n);
        ++stamp;
        for (uint32_t k = 0; k < n; ++k) faces[i][k] = PackedFace{uint32_t(i) << 24 | k, stamp};
        in[i].faces = &faces[i];
        auto cut = [&](int pieces, int dirBase) {
            std::vector<PreviewCluster> out;
            uint32_t at = 0;
            for (int p = 0; p < pieces && at < n; ++p) {
                const uint32_t left = n - at;
                const uint32_t c = p + 1 == pieces ? left : std::max<uint32_t>(1, left / uint32_t(pieces - p));
                PreviewCluster pc;
                pc.lo = glm::vec3(float(i), float(p), 0.0f);
                pc.hi = pc.lo + glm::vec3(1.0f);
                pc.firstFace = at;
                pc.count = c;
                pc.chunk = uint32_t(i);
                pc.dir = dirBase < 0 ? -1 : (dirBase + p) % 6;
                out.push_back(pc);
                at += c;
            }
            return out;
        };
        const int pieces = n == 0 ? 0 : 1 + int(rng() % 40u);
        in[i].clusters = cut(pieces, -1);
        in[i].clustersDir = cut(std::min<int>(int(n), pieces * 3), 0);
    };
    auto pick = [&](uint32_t cur) -> uint32_t {
        switch (rng() % 6u) {
            case 0: return 0u;
            case 1: return cur / 2;
            case 2: return cur + rng() % 50u;
            case 3: return cur + 2000u + rng() % 6000u;   // past any slack
            case 4: return 1u + rng() % 300u;
            default: return rng() % 9000u;
        }
    };
    for (size_t i = 0; i < nChunks; ++i) remake(i, 500u + rng() % 5000u);

    auto same = [](const PreviewCluster& a, const PreviewCluster& b) {
        return a.lo == b.lo && a.hi == b.hi && a.firstFace == b.firstFace && a.count == b.count &&
               a.chunk == b.chunk && a.level == b.level && a.leafMass == b.leafMass && a.dir == b.dir;
    };
    FacePool pool;
    Preview::Level L;
    PoolDirty d;
    pool.layout(in, L, d);
    ASSERT_TRUE(d.full);
    ASSERT_TRUE(pool.laidOut());

    auto check = [&](const char* when, int step) {
        SCOPED_TRACE(std::string(when) + " step " + std::to_string(step));
        ASSERT_EQ(L.firstFace.size(), nChunks);
        ASSERT_EQ(L.faceCount.size(), nChunks);
        ASSERT_EQ(L.faces.size(), pool.faceCapacity());
        ASSERT_EQ(L.clusters.size(), pool.clusterCapacity());
        ASSERT_EQ(L.clustersDir.size(), pool.dirCapacity());
        for (size_t i = 0; i < nChunks; ++i) {
            const FacePool::Spans& sp = pool.spans(i);
            ASSERT_EQ(L.firstFace[i], sp.faceFirst);
            ASSERT_EQ(L.faceCount[i], uint32_t(faces[i].size()));
            ASSERT_GE(sp.faceCap, uint32_t(faces[i].size()));
            ASSERT_LE(size_t(sp.faceFirst) + sp.faceCap, L.faces.size());
            for (size_t k = 0; k < faces[i].size(); ++k) {
                ASSERT_EQ(L.faces[sp.faceFirst + k].a, faces[i][k].a);
                ASSERT_EQ(L.faces[sp.faceFirst + k].b, faces[i][k].b);
            }
            auto slots = [&](const std::vector<PreviewCluster>& arr, const std::vector<PreviewCluster>& mine,
                             uint32_t first, uint32_t cap) {
                ASSERT_GE(cap, uint32_t(mine.size()));
                ASSERT_LE(size_t(first) + cap, arr.size());
                for (uint32_t k = 0; k < cap; ++k) {
                    const PreviewCluster& got = arr[first + k];
                    if (k < mine.size()) {
                        PreviewCluster want = mine[k];
                        want.firstFace += sp.faceFirst;
                        ASSERT_TRUE(same(got, want)) << "chunk " << i << " slot " << k;
                    } else {
                        ASSERT_EQ(got.count, 0u) << "chunk " << i << " slack slot " << k;
                    }
                }
            };
            slots(L.clusters, in[i].clusters, sp.clusterFirst, sp.clusterCap);
            slots(L.clustersDir, in[i].clustersDir, sp.dirFirst, sp.dirCap);
            for (size_t j = 0; j < i; ++j) {   // spans never overlap
                const FacePool::Spans& o = pool.spans(j);
                auto apart = [](uint32_t a, uint32_t an, uint32_t b, uint32_t bn) { return a + an <= b || b + bn <= a; };
                ASSERT_TRUE(apart(sp.faceFirst, sp.faceCap, o.faceFirst, o.faceCap)) << i << " vs " << j;
                ASSERT_TRUE(apart(sp.clusterFirst, sp.clusterCap, o.clusterFirst, o.clusterCap)) << i << " vs " << j;
                ASSERT_TRUE(apart(sp.dirFirst, sp.dirCap, o.dirFirst, o.dirCap)) << i << " vs " << j;
            }
        }
        // every live cluster draws inside its own chunk's faces
        for (const auto* arr : {&L.clusters, &L.clustersDir})
            for (const PreviewCluster& c : *arr) {
                if (c.count == 0) continue;
                ASSERT_LT(c.chunk, nChunks);
                const uint32_t f0 = L.firstFace[c.chunk], f1 = f0 + L.faceCount[c.chunk];
                ASSERT_GE(c.firstFace, f0);
                ASSERT_LE(c.firstFace + c.count, f1);
            }
    };
    check("layout", 0);

    size_t partial = 0, fulls = 0, dirtyFaces = 0, totalFaces = 0;
    for (int step = 1; step <= 400; ++step) {
        std::vector<size_t> changed;
        const size_t n = 1 + rng() % 2u;
        for (size_t k = 0; k < n; ++k) {
            const size_t i = rng() % nChunks;
            if (std::find(changed.begin(), changed.end(), i) != changed.end()) continue;
            remake(i, pick(uint32_t(faces[i].size())));
            changed.push_back(i);
        }
        const Preview::Level prev = L;
        std::vector<FacePool::Spans> before;
        for (size_t i = 0; i < nChunks; ++i) before.push_back(pool.spans(i));
        pool.update(in, changed, L, d);
        check("update", step);
        if (d.full) {
            ++fulls;
            continue;
        }
        ++partial;
        for (size_t i = 0; i < nChunks; ++i) {   // unchanged chunks never move
            if (std::find(changed.begin(), changed.end(), i) != changed.end()) continue;
            ASSERT_EQ(pool.spans(i).faceFirst, before[i].faceFirst) << "step " << step << " chunk " << i;
            ASSERT_EQ(pool.spans(i).clusterFirst, before[i].clusterFirst);
            ASSERT_EQ(pool.spans(i).dirFirst, before[i].dirFirst);
        }
        // the partial-upload contract: prev + the dirty ranges of the new image = the new image
        Preview::Level img = prev;
        ASSERT_EQ(img.faces.size(), L.faces.size());
        ASSERT_EQ(img.clusters.size(), L.clusters.size());
        ASSERT_EQ(img.clustersDir.size(), L.clustersDir.size());
        for (const PoolRange& r : d.faces) {
            ASSERT_LE(size_t(r.first) + r.count, L.faces.size());
            for (uint32_t k = 0; k < r.count; ++k) img.faces[r.first + k] = L.faces[r.first + k];
            dirtyFaces += r.count;
        }
        for (const PoolRange& r : d.clusters) {
            ASSERT_LE(size_t(r.first) + r.count, L.clusters.size());
            for (uint32_t k = 0; k < r.count; ++k) img.clusters[r.first + k] = L.clusters[r.first + k];
        }
        for (const PoolRange& r : d.clustersDir) {
            ASSERT_LE(size_t(r.first) + r.count, L.clustersDir.size());
            for (uint32_t k = 0; k < r.count; ++k) img.clustersDir[r.first + k] = L.clustersDir[r.first + k];
        }
        for (size_t i = 0; i < nChunks; ++i)
            for (uint32_t k = 0; k < L.faceCount[i]; ++k) {
                const size_t at = size_t(L.firstFace[i]) + k;
                ASSERT_EQ(img.faces[at].a, L.faces[at].a) << "step " << step << " chunk " << i << " face " << k;
                ASSERT_EQ(img.faces[at].b, L.faces[at].b);
            }
        for (size_t k = 0; k < L.clusters.size(); ++k) ASSERT_TRUE(same(img.clusters[k], L.clusters[k])) << k;
        for (size_t k = 0; k < L.clustersDir.size(); ++k) ASSERT_TRUE(same(img.clustersDir[k], L.clustersDir[k])) << k;
        for (size_t i = 0; i < nChunks; ++i) totalFaces += faces[i].size();
    }
    EXPECT_GT(partial, 300u);   // overflow re-layouts are the exception
    std::printf("[facepool] 400 steps: %zu partial, %zu full (%u layouts, %u moves); dirty faces %.2f %% of the live\n",
                partial, fulls, pool.layouts(), pool.moves(), totalFaces ? 100.0 * double(dirtyFaces) / totalFaces : 0.0);
}

// ---- F3.1 (VoxelFoundationPlan §9b): edit cost ------------------------------------------------------------------
// The row executor must read only the cells near the edit: a face is added only if its run meets the footprint grown
// by one, so the runs that touch that window (and the rows the head / stack tests read) are all it needs. Engine
// terrain at 1/8 m, 60 random 1 m brushes at and near the ground: the delta equals F2.16's whole-row executor, and the
// cells evaluated are <= 10 % of the whole-row count. Red on F2.16's executor (it IS the whole-row count).
TEST(VoxFoundMesher, RowRebuildReadsOnlyNearTheFootprint) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    VoxelArea area = surfaceArea(gen, 0, 0, 4);
    VoxelGrid& g = area.grid;
    const Edges e(Outside::Air);
    const CardSet cards;
    std::vector<PackedFace> faces = packedRuns(g, e, &cards);
    std::mt19937 rng(31u);
    auto rnd = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    const int nMat = int(area.palette.size());
    size_t cells = 0, whole = 0, added = 0;
    for (int it = 0; it < 60; ++it) {
        const int x = rnd(0, g.dim.x - 8), z = rnd(0, g.dim.z - 8);
        int top = g.dim.y - 1;
        while (top > 0 && g.mat[g.idx({x + 4, top, z + 4})] == 0) --top;
        const int y = std::clamp(top - 4 + rnd(-4, 4), 0, g.dim.y - 8);
        const glm::ivec3 lo(x, y, z), hi = lo + 7;
        const uint8_t m = rnd(0, 2) == 0 ? 0 : uint8_t(rnd(1, nMat - 1));
        for (int a = lo.x; a <= hi.x; ++a)
            for (int b = lo.y; b <= hi.y; ++b)
                for (int c = lo.z; c <= hi.z; ++c) g.set({a, b, c}, m);
        const RunsPlan plan = planRunsDelta(g.dim, faces, lo, hi);
        const RunsDelta ref = meshRunsPlanWholeRows(g, e, plan, &cards);
        const RunsDelta d = meshRunsPlan(g, e, plan, &cards);
        ASSERT_EQ(d.removed, ref.removed) << "edit " << it;
        ASSERT_EQ(sortedKeys(d.added), sortedKeys(ref.added)) << "edit " << it;
        cells += d.cellsEvaluated;
        whole += ref.cellsEvaluated;
        added += d.added.size();
        std::vector<char> gone(faces.size(), 0);
        for (uint32_t r : d.removed) gone[r] = 1;
        std::vector<PackedFace> next;
        for (size_t i = 0; i < faces.size(); ++i)
            if (!gone[i]) next.push_back(faces[i]);
        next.insert(next.end(), d.added.begin(), d.added.end());
        faces.swap(next);
    }
    ASSERT_EQ(sortedKeys(faces), sortedKeys(packedRuns(g, e, &cards)));   // the chain of deltas = a full rebuild
    std::printf("[f31 rows] 60 x 1 m edits on %dx%dx%d terrain: %zu cells evaluated vs %zu whole rows (%.2f %%), %zu "
                "faces added\n",
                g.dim.x, g.dim.y, g.dim.z, cells, whole, 100.0 * double(cells) / double(whole), added);
    EXPECT_LE(cells * 10, whole);
}

namespace {
// The reference flood (F2.10's stack flood, as connectivityFromBlocking was written then).
FaceConnect stackFlood(glm::ivec3 nb, const std::vector<uint8_t>& blocking) {
    auto bi = [&](glm::ivec3 b) { return size_t(b.z + nb.z * (b.y + nb.y * b.x)); };
    FaceConnect out{0, 0, 0, 0, 0, 0};
    std::vector<int> comp(blocking.size(), -1);
    const glm::ivec3 dir[6] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
    int id = 0;
    for (int bx = 0; bx < nb.x; ++bx)
        for (int by = 0; by < nb.y; ++by)
            for (int bz = 0; bz < nb.z; ++bz) {
                const glm::ivec3 s(bx, by, bz);
                if (blocking[bi(s)] || comp[bi(s)] != -1) continue;
                uint8_t f = 0;
                comp[bi(s)] = id;
                std::vector<glm::ivec3> st{s};
                while (!st.empty()) {
                    const glm::ivec3 c = st.back();
                    st.pop_back();
                    for (int a = 0; a < 3; ++a) {
                        if (c[a] == 0) f |= uint8_t(1u << (2 * a));
                        if (c[a] == nb[a] - 1) f |= uint8_t(1u << (2 * a + 1));
                    }
                    for (const glm::ivec3& dd : dir) {
                        const glm::ivec3 n = c + dd;
                        if (glm::any(glm::lessThan(n, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(n, nb))) continue;
                        if (blocking[bi(n)] || comp[bi(n)] != -1) continue;
                        comp[bi(n)] = id;
                        st.push_back(n);
                    }
                }
                for (int k = 0; k < 6; ++k)
                    if (f & (1u << k)) out[size_t(k)] |= f;
                ++id;
            }
    return out;
}
}  // namespace

// Connectivity is a function of the blocking bits alone, so the flood is owed only when a bit changed. (a) the change
// report of updateBlockBlocking is exact (red on a stub that always says "changed"); (b) connectivityFromBlocking
// equals the reference stack flood on random bits at 32^3 (a chunk) and odd sizes, densities 0-100 %.
TEST(VoxFoundMesher, ConnectivityFloodRunsOnlyWhenABlockingBitChanges) {
    std::mt19937 rng(3101u);
    auto rnd = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    CardSet see;
    see.set(4);
    VoxelGrid g;
    g.resize({64, 48, 40});
    for (int x = 0; x < 64; ++x)
        for (int z = 0; z < 40; ++z)
            for (int y = 0; y < 48; ++y) g.set({x, y, z}, y < 20 ? 1 : (y < 22 && rnd(0, 3) == 0 ? 2 : 0));
    std::vector<uint8_t> blocking = blockBlocking(g, see);
    size_t same = 0, changed = 0;
    for (int it = 0; it < 400; ++it) {
        glm::ivec3 lo, hi;
        const int size = rnd(0, 2) == 0 ? 8 : rnd(1, 6);
        for (int a = 0; a < 3; ++a) {
            lo[a] = size == 8 && rnd(0, 1) ? rnd(0, g.dim[a] / 8 - 1) * 8 : rnd(0, g.dim[a] - size);
            hi[a] = lo[a] + size - 1;
        }
        const uint8_t m = uint8_t(rnd(0, 4));
        for (int x = lo.x; x <= hi.x; ++x)
            for (int y = lo.y; y <= hi.y; ++y)
                for (int z = lo.z; z <= hi.z; ++z) g.set({x, y, z}, m);
        const std::vector<uint8_t> before = blocking;
        const bool ch = updateBlockBlocking(g, see, lo, hi, blocking);
        ASSERT_EQ(blocking, blockBlocking(g, see)) << "edit " << it;
        ASSERT_EQ(ch, blocking != before) << "edit " << it;
        (ch ? changed : same) += 1;
    }
    EXPECT_GT(same, 50u);
    EXPECT_GT(changed, 50u);
    const glm::ivec3 sizes[] = {{32, 32, 32}, {5, 3, 6}, {1, 1, 1}, {7, 32, 2}, {33, 4, 65}};
    for (const glm::ivec3& nb : sizes)
        for (int density : {0, 10, 30, 50, 70, 90, 100})
            for (int rep = 0; rep < 4; ++rep) {
                std::vector<uint8_t> b(size_t(nb.x) * nb.y * nb.z);
                for (uint8_t& v : b) v = rnd(0, 99) < density ? 1 : 0;
                ASSERT_EQ(connectivityFromBlocking(nb, b), stackFlood(nb, b))
                    << nb.x << "x" << nb.y << "x" << nb.z << " density " << density;
            }
    std::printf("[f31 connect] 400 edits: %zu changed a blocking bit, %zu did not (flood skipped)\n", changed, same);
}

// The reassembly after an edit writes only what changed into the Preview's drawn arrays: the rebuilt chunks' spans
// (the pool's dirty ranges) and the sprigs. Two whole chunks side by side (256^3 each, CPU meshRuns + foliageSprigs as
// the builder output), 40 random edits incl. across the shared border: after each, the drawn arrays equal the level
// image (what selectLevel copies whole), the sprigs equal the chunks' lists, and the bytes written are <= the dirty
// ranges + the sprigs. Red on F2.17's assemble (it copies the whole image every time).
TEST(VoxFoundMesher, RegionEditAssemblesOnlyWhatChanged) {
    constexpr int N = 256;
    std::mt19937 rng(3102u);
    auto rnd = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    CardSet cards;
    cards.set(4);
    std::vector<RegionChunk> rcs(2);
    for (int c = 0; c < 2; ++c) {
        RegionChunk& rc = rcs[size_t(c)];
        rc.chunk = {c, 0, 0};
        rc.originVoxel = {c * N, 0, 0};
        rc.grid.resize({N, N, N});
        for (int x = 0; x < N; ++x)
            for (int z = 0; z < N; ++z) {
                const int wx = c * N + x;
                const int h = 96 + int(12.0 * std::sin(wx * 0.05) + 9.0 * std::cos(z * 0.07));
                for (int y = 0; y < h; ++y) rc.grid.set({x, y, z}, uint8_t(y < h - 4 ? 1 : (y < h - 1 ? 2 : 3)));
            }
        for (int b = 0; b < 6; ++b) {   // leaf blobs
            const glm::ivec3 o(rnd(8, N - 9), rnd(110, 140), rnd(8, N - 9));
            const int r = rnd(3, 8);
            for (int x = -r; x <= r; ++x)
                for (int y = -r; y <= r; ++y)
                    for (int z = -r; z <= r; ++z)
                        if (x * x + y * y + z * z <= r * r) rc.grid.set(o + glm::ivec3(x, y, z), 4);
        }
        rc.border.resize({N, N, N}, false);
    }
    for (int y = 0; y < N; ++y)   // chunk 0's +x plane = chunk 1's x = 0 layer, and back
        for (int z = 0; z < N; ++z) {
            rcs[0].border.set(0, 1, y, z, rcs[1].grid.mat[rcs[1].grid.idx({0, y, z})] != 0);
            rcs[1].border.set(0, -1, y, z, rcs[0].grid.mat[rcs[0].grid.idx({N - 1, y, z})] != 0);
        }
    RegionEdit edit;
    edit.begin({0, 0, 0}, {1, 0, 0}, 4, true, 1u << 23);
    edit.setMaterialSets(cards, cards);
    for (int c = 0; c < 2; ++c) {
        const Edges e(rcs[size_t(c)].border);
        std::vector<PackedFace> out = packedRuns(rcs[size_t(c)].grid, e, &cards);
        SprigParams sp;
        sp.cards = cards;
        sp.worldOriginVoxel = rcs[size_t(c)].originVoxel;
        sp.threshold24 = 1u << 23;
        const std::vector<PackedFace> spr = foliageSprigs(rcs[size_t(c)].grid, e, sp);
        out.insert(out.end(), spr.begin(), spr.end());
        edit.addChunk(rcs[size_t(c)], out);
    }
    rcs.clear();   // the store owns the voxels now
    std::vector<uint16_t> leafTex(256);
    for (size_t i = 0; i < leafTex.size(); ++i) leafTex[i] = uint16_t(1000 + i);
    Preview pv;
    pv.chunks.resize(2);
    edit.assemble(pv, leafTex);
    auto sameCluster = [](const PreviewCluster& a, const PreviewCluster& b) {
        return a.lo == b.lo && a.hi == b.hi && a.firstFace == b.firstFace && a.count == b.count && a.chunk == b.chunk &&
               a.level == b.level && a.leafMass == b.leafMass && a.dir == b.dir;
    };
    size_t partial = 0, bytes = 0, imageBytes = 0, relaid = 0;
    uint32_t sprigLayoutsBefore = edit.sprigLayouts();
    for (int it = 0; it < 40; ++it) {
        glm::ivec3 lo;
        const int size = rnd(1, 16);
        lo.x = rnd(0, 2) == 0 ? rnd(N - size, N - 1) : rnd(0, 2 * N - size);   // a third straddle the border
        lo.y = rnd(80, 140);
        lo.z = rnd(0, N - size);
        const glm::ivec3 hi = lo + size - 1;
        const uint8_t m = uint8_t(rnd(0, 4));
        const RegionEdit::Stats st = edit.apply(lo, hi, [](glm::ivec3) { return true; }, m);
        edit.assemble(pv, leafTex, &st.rebuilt);
        const Preview::Level& L = pv.levels[0];
        const std::string at = "edit " + std::to_string(it);
        ASSERT_EQ(pv.faces.size(), L.faces.size()) << at;
        for (size_t i = 0; i < L.faces.size(); ++i) ASSERT_EQ(pv.faces[i].key(), L.faces[i].key()) << at << " face " << i;
        ASSERT_EQ(pv.clusters.size(), L.clusters.size()) << at;
        for (size_t i = 0; i < L.clusters.size(); ++i) ASSERT_TRUE(sameCluster(pv.clusters[i], L.clusters[i])) << at;
        ASSERT_EQ(pv.clustersDir.size(), L.clustersDir.size()) << at;
        for (size_t i = 0; i < L.clustersDir.size(); ++i)
            ASSERT_TRUE(sameCluster(pv.clustersDir[i], L.clustersDir[i])) << at;
        // each chunk's drawn sprig range = its list; ranges inside the array, in chunk order, never overlapping
        uint32_t prevEnd = 0;
        for (size_t c = 0; c < 2; ++c) {
            ASSERT_EQ(pv.chunks[c].firstFace, L.firstFace[c]) << at;
            ASSERT_EQ(pv.chunks[c].faceCount, L.faceCount[c]) << at;
            const PreviewChunk& pc = pv.chunks[c];
            ASSERT_GE(pc.firstSprig, prevEnd) << at;
            ASSERT_LE(size_t(pc.firstSprig) + pc.sprigCount, pv.sprigs.size()) << at;
            prevEnd = pc.firstSprig + pc.sprigCount;
            const std::vector<PackedFace>& mine = edit.chunk(c).sprigs;
            ASSERT_EQ(pc.sprigCount, uint32_t(mine.size())) << at;
            for (size_t k = 0; k < mine.size(); ++k) {
                const PackedFace& s = mine[k];
                ASSERT_EQ(pv.sprigs[pc.firstSprig + k].cell, (s.a & 0xFFFFFFu) | (15u << 24)) << at;
                ASSERT_EQ(pv.sprigs[pc.firstSprig + k].tex, uint32_t(leafTex[(s.b >> 16) & 0xFFu])) << at;
            }
            ASSERT_EQ(pc.connect, edit.chunk(c).connect) << at;
        }
        const uint32_t sprigLayoutsNow = edit.sprigLayouts();
        const bool sprigRelaid = sprigLayoutsNow != sprigLayoutsBefore;
        sprigLayoutsBefore = sprigLayoutsNow;
        if (pv.poolDirty.full || sprigRelaid) {   // an overflow re-places everything (counted, must stay rare)
            relaid += sprigRelaid ? 1 : 0;
            continue;
        }
        ++partial;
        size_t dirty = 0;
        for (const PoolRange& r : pv.poolDirty.faces) dirty += r.count * sizeof(PackedFace);
        for (const PoolRange& r : pv.poolDirty.clusters) dirty += r.count * sizeof(PreviewCluster);
        for (const PoolRange& r : pv.poolDirty.clustersDir) dirty += r.count * sizeof(PreviewCluster);
        // F3.1 p90: the sprigs too are spans with slack -- only the rebuilt chunks' sprig spans are written
        size_t sprigSpans = 0;
        for (size_t c : st.rebuilt) {
            const size_t end = c + 1 < pv.chunks.size() ? pv.chunks[c + 1].firstSprig : pv.sprigs.size();
            sprigSpans += (end - pv.chunks[c].firstSprig) * sizeof(PreviewSprig);
        }
        const size_t allowed = dirty + sprigSpans;
        ASSERT_LE(pv.lastCopyBytes, allowed) << at;
        bytes += pv.lastCopyBytes;
        imageBytes +=
            L.faces.size() * sizeof(PackedFace) + (L.clusters.size() + L.clustersDir.size()) * sizeof(PreviewCluster);
    }
    EXPECT_GT(partial, 30u);
    EXPECT_LE(relaid, 4u);
    std::printf("[f31 assemble] sprig spans re-placed %zu times\n", relaid);
    std::printf("[f31 assemble] %zu partial steps: %.1f KB written per step (the whole image is %.1f KB)\n", partial,
                partial ? double(bytes) / partial / 1024.0 : 0.0, partial ? double(imageBytes) / partial / 1024.0 : 0.0);
}
