// docs/VoxelFoundationPlan.md §4 + F2 — the CPU reference mesher (core/voxfound/Mesher.h).
//
// 1. Exact rigs with counts worked out by hand (written before the code ran).
// 2. Exactness: on random content and generated terrain, every merged mesh covers every exposed unit face
//    exactly once with the right material (coverageErrors == 0), at block limits 0 and 8.
// 3. MEASURED face counts (the F2 gate is "face count <= today's on the same scene"; this is the first look):
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
