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
#include <tuple>

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include "core/BuildingProgram.h"
#include "core/StructureGrid.h"
#include "core/StructureRealizer.h"
#include "core/StyleProfile.h"
#include "core/WorldGenerator.h"
#include "core/voxfound/Mesher.h"
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
    LodGrid l = lodBase(a.grid);
    const double vol0 = double(a.grid.solidCount());
    size_t faces0 = 0, prev = 0;
    std::map<std::string, double> share0;   // level 0's share of the up-facing area per material
    for (int k = 0; k <= 3; ++k) {
        if (k) l = lodDown(l);
        const size_t faces = meshRuns(l.grid, Outside::Skip, &cards).size();
        const double vol = double(l.grid.solidCount()) * double(1 << (3 * k));
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
        // gate (the look from above): every material holding >= 5 % of level 0's up-facing area keeps its share
        // within 10 percentage points at every level
        double total = 0.0;
        for (const auto& [name, m2] : up) total += m2;
        for (const auto& [name, m2] : up) {
            if (!k) share0[name] = m2 / total;
        }
        for (const auto& [name, s0] : share0) {
            if (s0 < 0.05) continue;
            const double sk = up.count(name) ? up[name] / total : 0.0;
            EXPECT_NEAR(sk, s0, 0.10) << "level " << k << ": " << name << " share of the ground seen from above";
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
