// VoxFoundStorageLabTest — docs/VoxelFoundationPlan.md §3 + §8 storage lab (F0 skeleton).
//
// Areas: the engine's terrain generator (WorldGenerator Perlin / Mountains, fixed seeds) sampled at 1/8 m by
// buildTerrainArea — engine-generated, not hand-placed; flora and structures are not in it yet (they are on
// thirds today and arrive with the content conversion, FC). Each area is one chunk footprint (32 x 32 m).
//
// Green now: the area is built as specified, and its block statistics are MEASURED (the §3.5a memory
// estimate was a guess; this replaces it with data for bare terrain). Prediction written in §3.5a before
// measuring: ~1-3 detailed blocks per block column on terrain.
// F1 (written red against the F0 stub, then built): the BlockStore equals the dense reference, collapses
// uniform blocks like the reference says it should, stays within the memory estimate, and keeps all of that
// under random edit churn.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <random>

#include "core/WorldGenerator.h"
#include "core/voxfound/StorageLab.h"

using namespace Phyxel;
using namespace Phyxel::Core::VoxFound;

namespace {

// An area covering the terrain surface of the 32 x 32 m footprint at (x0, z0), 8 m below the lowest ground to
// 8 m above the highest.
VoxelArea surfaceArea(WorldGenerator& gen, int x0, int z0) {
    float lo = std::numeric_limits<float>::max(), hi = -lo;
    for (int x = x0 - 1; x <= x0 + 32; ++x)
        for (int z = z0 - 1; z <= z0 + 32; ++z) {
            const float h = gen.sampleSurface(x, z).bedHeight;
            lo = std::min(lo, h);
            hi = std::max(hi, h);
        }
    const int y0 = int(std::floor(lo)) - 8;
    const int y1 = int(std::ceil(hi)) + 8;
    return buildTerrainArea(gen, {x0, y0, z0}, {32, y1 - y0, 32});
}

struct Named {
    const char* name;
    WorldGenerator::GenerationType type;
    uint32_t seed;
    int x, z;
};
const Named kAreas[] = {
    {"perlin s7 (0,0)", WorldGenerator::GenerationType::Perlin, 7u, 0, 0},
    {"perlin s7 (512,-256)", WorldGenerator::GenerationType::Perlin, 7u, 512, -256},
    {"perlin s12345 (-1024,768)", WorldGenerator::GenerationType::Perlin, 12345u, -1024, 768},
    {"mountains s7 (0,0)", WorldGenerator::GenerationType::Mountains, 7u, 0, 0},
    {"mountains s7 (2048,1024)", WorldGenerator::GenerationType::Mountains, 7u, 2048, 1024},
};

}  // namespace

TEST(VoxFoundStorageLab, TerrainAreaFollowsTheEngineGround) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    const VoxelArea a = surfaceArea(gen, 0, 0);
    ASSERT_GT(a.grid.solidCount(), 0u);
    ASSERT_GE(a.palette.size(), 2u);
    // at every metre column centre the 1/8 m ground top is within half a metre (+ one voxel) of today's cube top
    const glm::ivec3 o = a.originVoxel / kVoxelsPerM;
    double worst = 0;
    for (int mx = 0; mx < 32; ++mx)
        for (int mz = 0; mz < 32; ++mz) {
            const auto c = gen.sampleSurface(o.x + mx, o.z + mz);
            const int vx = mx * 8 + 4, vz = mz * 8 + 4;
            int top = -1;
            for (int vy = 0; vy < a.grid.dim.y; ++vy)
                if (a.grid.mat[a.grid.idx({vx, vy, vz})] != 0) top = vy;
            ASSERT_GE(top, 0);
            const double ourTop = double(o.y) + double(top + 1) / kVoxelsPerM;
            const double todayTop = double(c.surfaceY + 1);
            worst = std::max(worst, std::abs(ourTop - todayTop));
        }
    std::printf("[storage lab] worst |1/8 m ground top - today's cube top| at column centres: %.3f m\n", worst);
    EXPECT_LE(worst, 0.5 + 1.0 / kVoxelsPerM + 1e-6);
}

TEST(VoxFoundStorageLab, MeasuredBlockStatisticsOnGeneratedTerrain) {
    for (const Named& n : kAreas) {
        WorldGenerator gen(n.type, n.seed);
        const VoxelArea a = surfaceArea(gen, n.x, n.z);
        const BlockStats s = blockStats(a.grid);
        const double perCol = double(s.detailed) / double(s.blockColumns);
        const double dense = double(a.grid.size());
        std::printf("[storage lab] %-26s %3d m tall: %6zu blocks, %5zu detailed (%.2f per block column), %6zu air, "
                    "%6zu solid; est %.2f MB vs dense %.2f MB (%.1fx); materials %zu\n",
                    n.name, a.grid.dim.y / kVoxelsPerM, s.blocks, s.detailed, perCol, s.uniformAir, s.uniformSolid,
                    s.estimatedBytes() / 1e6, dense / 1e6, dense / double(s.estimatedBytes()), a.palette.size() - 1);
        EXPECT_EQ(s.blocks, s.uniformAir + s.uniformSolid + s.detailed);
        EXPECT_GT(s.detailed, 0u);
    }
}

// The §3.5a prediction was "~1-3 detailed blocks per block column". First run (2026-11-10, this file):
// Perlin 0.86 / 0.98 / 1.06, Mountains 1.42 / 2.25 — the LOWER bound was wrong (bare terrain is cheaper than
// predicted); recorded in the plan. What the memory budget relies on is the UPPER bound, which held, so that is
// what stays asserted, on every area. Bare terrain only: flora, structures and water will raise it (FC/F1).
TEST(VoxFoundStorageLab, PredictionTerrainHasAtMostThreeDetailedBlocksPerColumn) {
    for (const Named& n : kAreas) {
        WorldGenerator gen(n.type, n.seed);
        const BlockStats s = blockStats(surfaceArea(gen, n.x, n.z).grid);
        const double perCol = double(s.detailed) / double(s.blockColumns);
        EXPECT_GT(perCol, 0.5) << n.name;
        EXPECT_LE(perCol, 3.0) << n.name;
    }
}

// ---- Flora (FC generators -> storage cost) ----------------------------------------------------
// The shipped biomes (resources/biomes.json, found by walking up from the working directory: the test binary
// runs in build/tests, where the generator's CWD-relative load finds nothing and falls back to built-in biomes;
// the F0 terrain numbers above were measured that way). Trees: the engine's planFlora, stamped from the
// tree_forge --grid 8 library (resources/templates_g8/nature, generator output). Pooled plants with no grid-8
// template yet (gen_tree.py bushes, hand-authored fern/apple/flower, "voxel matrix" shrubs) are counted as
// missing and NOT stamped, so these numbers are a lower bound for undergrowth.
//
// PREDICTION (written 2026-11-11 before the first run): in a footprint holding >= 10 stamped trees, flora
// raises detailed blocks per column from ~1 to 4-10, and the estimated store for the chunk footprint stays
// <= 6 MB (which keeps a 256 m radius, ~200 footprints, near 1.2 GB: inside the §3.5a 3 GB budget).

namespace {
std::string repoRoot() {
    namespace fs = std::filesystem;
    for (fs::path p = fs::current_path(); !p.empty(); p = p.parent_path()) {
        if (fs::exists(p / "resources" / "biomes.json") && fs::exists(p / "resources" / "templates_g8")) return p.string();
        if (p == p.parent_path()) break;
    }
    return {};
}
}  // namespace

TEST(VoxFoundStorageLab, MeasuredFloraCostOnShippedBiomes) {
    const std::string root = repoRoot();
    ASSERT_FALSE(root.empty()) << "repo root (resources/biomes.json + resources/templates_g8) not found from cwd";
    const std::string dir = root + "/resources/templates_g8/nature";
    struct Spot { std::string name; WorldGenerator::GenerationType type; uint32_t seed; int x, z; };
    std::vector<Spot> spots;
    for (const Named& n : kAreas) spots.push_back({n.name, n.type, n.seed, n.x, n.z});
    // plus the first footprints along a line (every 160 m) whose plan holds >= 10 pool trees (selected on the
    // INPUT: tree count; the measured cost is not looked at when choosing)
    {
        WorldGenerator g(WorldGenerator::GenerationType::Perlin, 7u);
        ASSERT_TRUE(g.loadBiomes(root + "/resources/biomes.json"));
        int found = 0;
        for (int k = 0; k < 64 && found < 3; ++k) {
            const int x = k * 160, z = 96;
            const auto plan = g.planFlora(x, z, x + 31, z + 31, 0);
            int trees = 0;
            for (const auto& p : plan) trees += (p.templateName.rfind("forge_", 0) == 0 && p.templateName.find("bush") == std::string::npos);
            if (trees >= 10) {


                spots.push_back({"perlin s7 forest (" + std::to_string(x) + "," + std::to_string(z) + ")",
                                 WorldGenerator::GenerationType::Perlin, 7u, x, z});
                ++found;
            }
        }
        EXPECT_GE(found, 1) << "no forested footprint found along the search line";
    }
    for (const Spot& s : spots) {
        WorldGenerator gen(s.type, s.seed);
        ASSERT_TRUE(gen.loadBiomes(root + "/resources/biomes.json"));
        // headroom: the tallest template the plan stamps here, + 2 m
        int tallM = 0;
        for (const auto& p : gen.planFlora(s.x - 24, s.z - 24, s.x + 55, s.z + 55, 0)) {
            const GridTemplate t = loadGridTemplate(dir + "/" + p.templateName + ".voxel");
            if (t.error.empty()) tallM = std::max(tallM, (t.bounds.y + kVoxelsPerM - 1) / kVoxelsPerM);
        }
        float lo = std::numeric_limits<float>::max(), hi = -lo;
        for (int x = s.x - 1; x <= s.x + 32; ++x)
            for (int z = s.z - 1; z <= s.z + 32; ++z) {
                const float h = gen.sampleSurface(x, z).bedHeight;
                lo = std::min(lo, h);
                hi = std::max(hi, h);
            }
        const int y0 = int(std::floor(lo)) - 8, y1 = int(std::ceil(hi)) + 8 + tallM;
        VoxelArea a = buildTerrainArea(gen, {s.x, y0, s.z}, {32, y1 - y0, 32});
        const BlockStats bare = blockStats(a.grid);
        const FloraStampStats f = stampFlora(a, gen, dir);
        const BlockStats with = blockStats(a.grid);
        const double cols = double(with.blockColumns);
        std::string miss;
        for (const auto& m : f.missingNames) miss += m + " ";
        std::printf("[storage lab] %-30s trees/plants planned %zu (stamped %zu, procedural %zu, missing %zu: %s)\n"
                    "              detailed per column bare %.2f -> with flora %.2f; est %.2f MB -> %.2f MB per chunk "
                    "footprint (%d m tall area)\n",
                    s.name.c_str(), f.placements, f.stamped, f.procedural, f.missing, miss.c_str(), bare.detailed / cols,
                    with.detailed / cols, bare.estimatedBytes() / 1e6, with.estimatedBytes() / 1e6, y1 - y0);
        EXPECT_EQ(f.procedural, 0u) << s.name << ": procedural flora is not on the grid";
        // The prediction is about TREES. First run gated on all stamped plants (f.stamped), so a footprint of
        // 10 mostly-bush plants was held to the forest bound; corrected to count trees (forge_*, not bushes).
        int trees = 0;
        for (const auto& [name, n] : f.stampedByName)
            if (name.rfind("forge_", 0) == 0 && name.find("bush") == std::string::npos) trees += n;
        std::printf("              stamped trees %d\n", trees);
        // Second run: the LOWER bound was wrong for a sparse footprint (Perlin s7 (512,-256): 10 trees, 2.27 per
        // column); it held in the two forests (59 and 65 trees: 5.84 and 6.04). Recorded in the plan (§3.5a). The
        // budget relies on the UPPER bounds, which held everywhere, so those stay asserted.
        if (trees >= 10) {
            EXPECT_LE(with.detailed / cols, 10.0) << s.name;
            EXPECT_LE(with.estimatedBytes(), size_t(6e6)) << s.name;
        }
    }
}

// ---- F1: the block store against the dense reference ---------------------------------------
// Written red against the F0 stub before BlockStore had a body. collapse() was added to the interface in F1
// (2026-11-11): it ends an edit batch (the per-tick changed-block list of §3.2).

TEST(VoxFoundStorageLab, BlockStoreEqualsTheDenseReference) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    const VoxelArea a = surfaceArea(gen, 0, 0);
    BlockStore store(a.grid.dim);
    for (size_t i = 0; i < a.grid.size(); ++i) store.set(a.grid.pos(i), a.grid.mat[i]);
    store.collapse();
    std::string why;
    EXPECT_EQ(compareToReference(store, a.grid, &why), 0u) << why;
}

TEST(VoxFoundStorageLab, BlockStoreCollapsesUniformBlocks) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    const VoxelArea a = surfaceArea(gen, 0, 0);
    const BlockStats s = blockStats(a.grid);
    BlockStore store(a.grid.dim);
    for (size_t i = 0; i < a.grid.size(); ++i) store.set(a.grid.pos(i), a.grid.mat[i]);
    store.collapse();
    EXPECT_EQ(store.uniformBlocks(), s.uniformAir + s.uniformSolid);
    EXPECT_EQ(store.detailedBlocks(), s.detailed);
}

TEST(VoxFoundStorageLab, BlockStoreMemoryWithinTheEstimate) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    const VoxelArea a = surfaceArea(gen, 0, 0);
    const BlockStats s = blockStats(a.grid);
    BlockStore store(a.grid.dim);
    for (size_t i = 0; i < a.grid.size(); ++i) store.set(a.grid.pos(i), a.grid.mat[i]);
    store.collapse();
    EXPECT_GT(store.memoryBytes(), 0u);
    EXPECT_LE(double(store.memoryBytes()), 1.1 * double(s.estimatedBytes()));
}

TEST(VoxFoundStorageLab, BlockStoreOutOfRangeReadsAir) {
    BlockStore store({16, 16, 16});
    store.set({3, 3, 3}, 7);
    store.collapse();
    EXPECT_EQ(store.get({3, 3, 3}), 7);
    EXPECT_EQ(store.get({-1, 0, 0}), 0);
    EXPECT_EQ(store.get({16, 0, 0}), 0);
    EXPECT_EQ(store.get({0, 0, 99}), 0);
}

// Stress (churn): random brush edits (1, 2, 4, 8 voxels, aligned and unaligned, including erasing to air) on a
// 64^3 area of engine terrain. After every batch the store must equal the reference voxel for voxel, its
// uniform/detailed counts must equal blockStats(reference), the solidity summary must be right for every
// block, and memory must stay within the estimate. 200 batches x 40 edits.
// Prediction: holds at every batch (the store is exact, not approximate).
TEST(VoxFoundStorageLab, BlockStoreChurnMatchesReferenceAtEveryBatch) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    const float ground = gen.sampleSurface(4, 4).bedHeight;
    const VoxelArea src = buildTerrainArea(gen, {0, int(std::floor(ground)) - 4, 0}, {8, 8, 8});
    VoxelGrid ref = src.grid;
    ASSERT_GT(ref.solidCount(), 0u);
    ASSERT_LT(ref.solidCount(), ref.size());
    BlockStore store(ref.dim);
    for (size_t i = 0; i < ref.size(); ++i) store.set(ref.pos(i), ref.mat[i]);
    store.collapse();

    std::mt19937 rng(12345u);
    const int sizes[] = {1, 2, 4, 8};
    const glm::ivec3 nb = ref.dim / kBlock;
    for (int batch = 0; batch < 200; ++batch) {
        for (int e = 0; e < 40; ++e) {
            const int s = sizes[rng() % 4];
            const bool aligned = (rng() % 2) == 0;
            glm::ivec3 o{int(rng() % 64), int(rng() % 64), int(rng() % 64)};
            if (aligned) o = (o / s) * s;
            const uint8_t m = uint8_t(rng() % 4);   // 0 = erase
            for (int x = 0; x < s; ++x)
                for (int y = 0; y < s; ++y)
                    for (int z = 0; z < s; ++z) {
                        const glm::ivec3 p = o + glm::ivec3(x, y, z);
                        if (!ref.in(p)) continue;
                        ref.mat[ref.idx(p)] = m;
                        store.set(p, m);
                    }
        }
        store.collapse();
        std::string why;
        ASSERT_EQ(compareToReference(store, ref, &why), 0u) << "batch " << batch << ": " << why;
        const BlockStats st = blockStats(ref);
        ASSERT_EQ(store.uniformBlocks(), st.uniformAir + st.uniformSolid) << "batch " << batch;
        ASSERT_EQ(store.detailedBlocks(), st.detailed) << "batch " << batch;
        for (int bx = 0; bx < nb.x; ++bx)
            for (int by = 0; by < nb.y; ++by)
                for (int bz = 0; bz < nb.z; ++bz) {
                    bool any = false;
                    for (int i = 0; i < kBlock * kBlock * kBlock && !any; ++i)
                        any = ref.at({bx * kBlock + i / 64, by * kBlock + (i / 8) % 8, bz * kBlock + i % 8}) != 0;
                    ASSERT_EQ(store.blockSolidAny({bx, by, bz}), any) << "batch " << batch;
                }
        ASSERT_LE(double(store.memoryBytes()), 1.1 * double(st.estimatedBytes())) << "batch " << batch;
    }
}


// ---- F2.8: a region generated chunk by chunk equals the region generated whole ------------------
// CLAUDE.md "chunks must not be visible": the live preview generates one 32 m chunk (+1 m margin) at a time.
// Every voxel of every chunk, and every chunk's border solidity, must equal the same region generated as ONE
// area. Engine content: WorldGenerator Perlin seed 7 (the F2.2 forest, chunks x 255..256, z 3..4) + the
// engine's flora plan stamped from resources/templates_g8/nature.
TEST(VoxFoundStorageLab, RegionChunksEqualTheRegionGeneratedWhole) {
    const std::string root = repoRoot();
    ASSERT_FALSE(root.empty());
    const std::string dir = root + "/resources/templates_g8/nature";
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 7u);
    ASSERT_TRUE(gen.loadBiomes(root + "/resources/biomes.json"));
    const int cx0 = 255, cx1 = 256, cz0 = 3, cz1 = 4;
    float gLo = std::numeric_limits<float>::max(), gHi = -gLo;
    for (int x = cx0 * kChunkM; x < (cx1 + 1) * kChunkM; ++x)
        for (int z = cz0 * kChunkM; z < (cz1 + 1) * kChunkM; ++z) {
            const float h = gen.sampleSurface(x, z).bedHeight;
            gLo = std::min(gLo, h);
            gHi = std::max(gHi, h);
        }
    const int cy0 = int(std::floor((std::floor(gLo) - 2) / float(kChunkM)));
    const int cy1 = int(std::floor((std::ceil(gHi) + 26) / float(kChunkM)));
    const glm::ivec3 cLo(cx0, cy0, cz0), cHi(cx1, cy1, cz1), nC = cHi - cLo + 1;
    std::printf("[region] ground %.1f..%.1f m -> chunks y %d..%d (%d chunks)\n", gLo, gHi, cy0, cy1,
                nC.x * nC.y * nC.z);

    VoxelArea whole = buildTerrainArea(gen, cLo * kChunkM, nC * kChunkM);
    const FloraStampStats wf = stampFlora(whole, gen, dir);
    ASSERT_GT(wf.stamped, 10u) << "the forest has trees";
    const int n = kChunkM * kVoxelsPerM;
    const glm::ivec3 wd = whole.grid.dim;
    auto wholeSolid = [&](glm::ivec3 p) { return whole.grid.mat[whole.grid.idx(p)] != 0; };
    size_t badVox = 0, badBorder = 0, checkedBorder = 0, solidSeen = 0;
    std::string first;
    const RegionBuild rb = forEachRegionChunk(gen, cLo, cHi, &dir, [&](const RegionChunk& rc,
                                                                       const std::vector<std::string>& pal) {
        const glm::ivec3 off = (rc.chunk - cLo) * n;
        EXPECT_EQ(rc.originVoxel, rc.chunk * n);
        for (int x = 0; x < n; ++x)
            for (int y = 0; y < n; ++y)
                for (int z = 0; z < n; ++z) {
                    const glm::ivec3 p(x, y, z);
                    const std::string& want = whole.palette[whole.grid.mat[whole.grid.idx(off + p)]];
                    const std::string& got = pal[rc.grid.mat[rc.grid.idx(p)]];
                    solidSeen += got != "air";
                    if (want != got && badVox++ < 3)
                        first += "chunk (" + std::to_string(rc.chunk.x) + "," + std::to_string(rc.chunk.y) + "," +
                                 std::to_string(rc.chunk.z) + ") voxel (" + std::to_string(x) + "," +
                                 std::to_string(y) + "," + std::to_string(z) + ") whole " + want + " chunk " + got +
                                 "; ";
                }
        // border planes that face another chunk of the region: the outside voxel is in `whole`
        for (int axis = 0; axis < 3; ++axis)
            for (int dir = -1; dir <= 1; dir += 2) {
                const int a = (dir < 0 ? -1 : n) + off[axis];
                if (a < 0 || a >= wd[axis]) continue;
                const int ua = (axis + 1) % 3, va = (axis + 2) % 3;
                for (int u = 0; u < n; ++u)
                    for (int v = 0; v < n; ++v) {
                        glm::ivec3 q;
                        q[axis] = a;
                        q[ua] = off[ua] + u;
                        q[va] = off[va] + v;
                        ++checkedBorder;
                        badBorder += rc.border.solid(axis, dir, u, v) != wholeSolid(q);
                    }
            }
    });
    std::printf("[region] %zu chunks, %zu solid voxels, whole-region flora stamped %zu; voxel mismatches %zu, "
                "border mismatches %zu of %zu\n",
                rb.chunks, solidSeen, wf.stamped, badVox, badBorder, checkedBorder);
    EXPECT_EQ(rb.chunks, size_t(nC.x * nC.y * nC.z));
    EXPECT_GT(checkedBorder, 0u);
    EXPECT_EQ(badVox, 0u) << first;
    EXPECT_EQ(badBorder, 0u);
}
