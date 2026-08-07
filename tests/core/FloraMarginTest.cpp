// Megaflora Increment B — red-before-green for the flora decoration margin.
//
// ObjectTemplateManager::decorateChunk() only considers flora whose trunk column is within a
// fixed margin of the chunk (historically kMargin = 12). Any template whose canopy is wider than
// that margin is SILENTLY CLIPPED at chunk seams: a chunk the canopy overhangs into, but whose
// trunk sits > margin columns away, never sees the placement in its planFlora window and so never
// stamps its slice. That caps usable canopy radius at 12 cubes and is the #1 blocker for giant
// trees (see docs/ProceduralTreeExpansionPlan.md, Increment B).
//
// This test stamps a WIDE template (half-footprint 24 cubes) two ways over the same multi-chunk
// region: (a) a whole-region reference set = every template voxel that lands inside the region's
// chunks, and (b) the per-chunk union produced by decorateChunk. They must be IDENTICAL. With the
// too-small margin, (b) is a strict subset of (a) — the far-side canopy slices are missing — and
// the equality assertion fails RED. After the margin is driven by the actual template footprint,
// they match GREEN.

#include <gtest/gtest.h>
#include "core/WorldGenerator.h"
#include "core/ObjectTemplateManager.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "core/VoxelTemplate.h"
#include "utils/CoordinateUtils.h"
#include <glm/glm.hpp>
#include <fstream>
#include <filesystem>
#include <set>
#include <cmath>

namespace Phyxel {
namespace {

namespace fs = std::filesystem;

constexpr int CS = 32;
constexpr int R = 24;               // canopy radius in cubes = the Increment-B footprint cap
constexpr int CANOPY_Y = 2;         // template canopy layers (y = 0..CANOPY_Y-1)

struct TempFiles {
    fs::path dir, tmpl, biomes;
    TempFiles() = default;
    TempFiles(const TempFiles&) = delete;
    // Move-only: a moved-from instance must NOT delete the dir (its destructor would otherwise
    // wipe the fixtures before the returned copy is used).
    TempFiles(TempFiles&& o) noexcept
        : dir(std::move(o.dir)), tmpl(std::move(o.tmpl)), biomes(std::move(o.biomes)) {
        o.dir.clear();
    }
    ~TempFiles() {
        if (!dir.empty()) { std::error_code ec; fs::remove_all(dir, ec); }
    }
};

// A radius-R disc of Leaf cubes (2 layers). Half-footprint = R on both x and z, so a plant near a
// chunk seam overhangs R cubes into neighbors — far past the old 12-column margin.
TempFiles writeFixtures() {
    TempFiles t;
    t.dir = fs::temp_directory_path() / "phyxel_flora_margin_test";
    fs::create_directories(t.dir);
    t.tmpl = t.dir / "_flora_wide.voxel";
    t.biomes = t.dir / "_biomes.json";

    std::ofstream vf(t.tmpl);
    vf << "# name: _flora_wide\n";
    for (int x = 0; x <= 2 * R; ++x)
        for (int z = 0; z <= 2 * R; ++z) {
            double d = std::hypot(x - R, z - R);
            if (d <= R)
                for (int y = 0; y < CANOPY_Y; ++y)
                    vf << "C " << x << ' ' << y << ' ' << z << " Leaf\n";
        }
    vf.close();

    std::ofstream bf(t.biomes);
    bf << R"({"biomes":[{"name":"WideTest","surface":"Grass","subsurface":"Dirt","deep":"Stone",)"
          R"("temp":[0.0,1.0],"moisture":[0.0,1.0],"heightScale":0.0,"heightOffset":0.0,)"
          R"("flora":{"density":1.0,"spacing":8,"mode":"pool",)"
          R"("items":[{"template":"_flora_wide","weight":1}]}}]})";
    bf.close();
    return t;
}

// Max cube extent (mirrors decorateChunk's centering: base = worldX - mx/2).
glm::ivec3 maxExtent(const VoxelTemplate& t) {
    glm::ivec3 mx(0);
    for (const auto& c : t.cubes) mx = glm::max(mx, c.relativePos);
    return mx;
}

class FloraMarginTest : public ::testing::Test {
protected:
    static constexpr int CX0 = 0, CX1 = 5, CZ0 = 0, CZ1 = 5;  // 6x6 chunks, cy = 0
    bool inRegion(const glm::ivec3& w) const {
        return w.x >= CX0 * CS && w.x < (CX1 + 1) * CS &&
               w.z >= CZ0 * CS && w.z < (CZ1 + 1) * CS &&
               w.y >= 0 && w.y < CS;
    }
};

TEST_F(FloraMarginTest, WideCanopyNotClippedAtChunkSeams) {
    TempFiles fx = writeFixtures();

    WorldGenerator gen(WorldGenerator::GenerationType::Flat, 4242);
    ASSERT_TRUE(gen.loadBiomes(fx.biomes.string())) << "temp biomes.json failed to load";

    ChunkManager cm;
    ObjectTemplateManager otm(&cm, nullptr);
    ASSERT_TRUE(otm.loadTemplate(fx.tmpl.string()));
    const VoxelTemplate* wide = otm.getTemplate("_flora_wide");
    ASSERT_NE(wide, nullptr);
    ASSERT_FALSE(wide->cubes.empty());
    const glm::ivec3 mx = maxExtent(*wide);
    ASSERT_EQ(mx.x, 2 * R);   // confirm the fixture really is 24-radius (half-footprint 24)

    // The margin must have grown to cover this template's footprint (was a hardcoded 12).
    EXPECT_EQ(otm.floraMarginColumns(), R)
        << "flora margin did not grow to the loaded template's half-footprint";

    // (a) Reference: every template cube of every planned plant whose canopy can reach the region.
    // Query is expanded by R so plants rooted just OUTSIDE the region (but overhanging in) are
    // included too — otherwise per-chunk (which does see them) would show spurious EXTRA voxels
    // that mask the seam loss. This set is the ground-truth "correct" stamp of the region.
    auto placements = gen.planFlora(CX0 * CS - R, CZ0 * CS - R,
                                    (CX1 + 1) * CS - 1 + R, (CZ1 + 1) * CS - 1 + R, 0);
    ASSERT_GT(placements.size(), 0u) << "no flora planned — fixture/biome misconfigured";
    std::set<std::tuple<int, int, int>> reference;
    for (const auto& p : placements) {
        glm::ivec3 base(p.worldX - mx.x / 2, p.surfaceY + 1, p.worldZ - mx.z / 2);
        for (const auto& c : wide->cubes) {
            glm::ivec3 w = base + c.relativePos;
            if (inRegion(w)) reference.insert({w.x, w.y, w.z});
        }
    }
    ASSERT_GT(reference.size(), 0u);

    // (b) Per-chunk union: what decorateChunk actually stamps, chunk by chunk.
    std::set<std::tuple<int, int, int>> perChunk;
    for (int cx = CX0; cx <= CX1; ++cx)
        for (int cz = CZ0; cz <= CZ1; ++cz) {
            glm::ivec3 coord(cx, 0, cz);
            Chunk chunk(coord * CS);
            chunk.initializeForLoading();
            otm.decorateChunk(chunk, coord, gen);
            const glm::ivec3 origin = coord * CS;
            for (int lx = 0; lx < CS; ++lx)
                for (int ly = 0; ly < CS; ++ly)
                    for (int lz = 0; lz < CS; ++lz)
                        if (chunk.getCubeAt({lx, ly, lz}) != nullptr)
                            perChunk.insert({origin.x + lx, origin.y + ly, origin.z + lz});
        }

    // The whole point: per-chunk stamping must reproduce the whole-region reference exactly.
    // RED (margin 12 < radius 24): perChunk is missing every far-side canopy slice.
    EXPECT_EQ(perChunk.size(), reference.size())
        << "per-chunk stamping lost "
        << (static_cast<long long>(reference.size()) - static_cast<long long>(perChunk.size()))
        << " canopy voxels at chunk seams (margin too small for a radius-" << R << " canopy)";
    EXPECT_EQ(perChunk, reference)
        << "per-chunk stamped voxel set differs from the whole-region reference";
}

// Writes a biome-only fixture whose flora is a single named (already-shipped) template.
fs::path writeBiomeFor(const fs::path& dir, const std::string& templateName) {
    fs::path biomes = dir / "_biomes.json";
    std::ofstream bf(biomes);
    bf << R"({"biomes":[{"name":"GiantTest","surface":"Grass","subsurface":"Dirt","deep":"Stone",)"
          R"("temp":[0.0,1.0],"moisture":[0.0,1.0],"heightScale":0.0,"heightOffset":0.0,)"
          R"("flora":{"density":1.0,"spacing":10,"mode":"pool","items":[{"template":")"
       << templateName << R"(","weight":1}]}}]})";
    return biomes;
}

// A 120-cube-tall giant spans ~4-5 VERTICAL chunks. Streaming decorates each chunk independently
// (including the vertical stack), so the giant's per-chunk y-slices must union back to the exact
// whole-tree stamp — the y=31->32 seam is a known trap class (docs/AgentContext.md). This exercises
// BOTH the x/z footprint margin AND the vertical seam at once, on the real shipped redwood template.
TEST_F(FloraMarginTest, GiantSpansVerticalChunksNoSeam) {
    const std::string kGiant = "tree_redwood_xl";  // 120 cubes tall, cubes-only (no sub/micro)
    TempFiles fx;
    fx.dir = fs::temp_directory_path() / "phyxel_giant_yseam_test";
    fs::create_directories(fx.dir);
    fx.biomes = writeBiomeFor(fx.dir, kGiant);

    WorldGenerator gen(WorldGenerator::GenerationType::Flat, 909);
    ASSERT_TRUE(gen.loadBiomes(fx.biomes.string()));

    ChunkManager cm;
    ObjectTemplateManager otm(&cm, nullptr);
    ASSERT_TRUE(otm.loadTemplate("resources/templates/nature/" + kGiant + ".voxel"))
        << "shipped giant template missing — run gen_tree.py --batch tools/tree_library.json";
    const VoxelTemplate* g = otm.getTemplate(kGiant);
    ASSERT_NE(g, nullptr);
    ASSERT_TRUE(g->subcubes.empty() && g->microcubes.empty())
        << "this test scans cubes only; redwood must be cube-resolution";
    const glm::ivec3 mx = maxExtent(*g);
    // The giant must cross at least one vertical chunk seam (placed near y~18, height mx.y, it spans
    // multiple 32-tall chunks). Broad world-trees are ~46-56 cubes tall → 2-3 vertical chunks.
    ASSERT_GE(mx.y, CS) << "giant too short to exercise a vertical seam (mx.y=" << mx.y << ")";

    // Region: enough x/z to hold a plant, and the full vertical stack the canopy reaches.
    const int CY_MAX = (mx.y + CS) / CS + 1;   // vertical chunks to cover the tree's height
    const int marginR = otm.floraMarginColumns();
    auto placements = gen.planFlora(CX0 * CS - marginR, CZ0 * CS - marginR,
                                    (CX1 + 1) * CS - 1 + marginR, (CZ1 + 1) * CS - 1 + marginR, 0);
    ASSERT_GT(placements.size(), 0u);

    auto inBox = [&](const glm::ivec3& w) {
        return w.x >= CX0 * CS && w.x < (CX1 + 1) * CS &&
               w.z >= CZ0 * CS && w.z < (CZ1 + 1) * CS &&
               w.y >= 0 && w.y < (CY_MAX + 1) * CS;
    };

    std::set<std::tuple<int, int, int>> reference;
    for (const auto& p : placements) {
        glm::ivec3 base(p.worldX - mx.x / 2, p.surfaceY + 1, p.worldZ - mx.z / 2);
        for (const auto& c : g->cubes) {
            glm::ivec3 w = base + c.relativePos;
            if (inBox(w)) reference.insert({w.x, w.y, w.z});
        }
    }
    ASSERT_GT(reference.size(), 0u);

    std::set<std::tuple<int, int, int>> perChunk;
    for (int cx = CX0; cx <= CX1; ++cx)
        for (int cz = CZ0; cz <= CZ1; ++cz)
            for (int cy = 0; cy <= CY_MAX; ++cy) {   // <-- the vertical stack
                glm::ivec3 coord(cx, cy, cz);
                Chunk chunk(coord * CS);
                chunk.initializeForLoading();
                otm.decorateChunk(chunk, coord, gen);
                const glm::ivec3 origin = coord * CS;
                for (int lx = 0; lx < CS; ++lx)
                    for (int ly = 0; ly < CS; ++ly)
                        for (int lz = 0; lz < CS; ++lz)
                            if (chunk.getCubeAt({lx, ly, lz}) != nullptr)
                                perChunk.insert({origin.x + lx, origin.y + ly, origin.z + lz});
            }

    EXPECT_EQ(perChunk.size(), reference.size())
        << "vertical per-chunk stamping lost "
        << (static_cast<long long>(reference.size()) - static_cast<long long>(perChunk.size()))
        << " voxels (x/z margin or y-seam clip on a " << mx.y << "-cube-tall giant)";
    EXPECT_EQ(perChunk, reference) << "giant per-chunk stamp differs from whole-tree reference";
}

}  // namespace
}  // namespace Phyxel
