#include <gtest/gtest.h>

#include "core/BasinRimCorrector.h"
#include "core/HydrologyMap.h"
#include "core/WaterBodyIndex.h"
#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Phyxel;

// ============================================================================
// BasinRimTest — M2c (docs/WorldForgeV2Plan.md, M2 engineering design): the owner's acceptance
// test for "water tied to the terrain holding it", made true by construction. For every basin
// checked, no rim column (a dry column bordering a wet column) has its voxel surface below the
// level it borders — the exact rule WaterManager::validateTable counts as a rim leak, evaluated
// here on the generator's own output (sampleSurface), no engine needed.
//
// RED on the committed code (2026-10-21): the baseline world's first above-sea lake validates at
// 418 leaks of 543 rim columns in the engine; here the same rule over the nearest basins.
// ============================================================================
namespace {

WorldRecipe v2Recipe(uint32_t seed) {
    WorldRecipe r;
    r.version = 2;
    r.type = "Perlin";
    r.seed = seed;
    return r;
}

struct RimCheck { long rim = 0, leaks = 0; int worst = 0; int worstX = 0, worstZ = 0; };

// The validator's rule over one basin: wet cells of the basin (corrected level > NO_WATER), their
// dry 4-neighbours' edge rows of columns, voxel surface vs floor(level).
RimCheck checkBasin(WorldGenerator& g, const WaterBodyIndex::Body& b) {
    RimCheck rc;
    const HydrologyMap* h = g.hydrology();
    const WaterBodyIndex* wb = g.waterBodies();
    const int cs = static_cast<int>(std::lround(h->cellSize()));
    const int ox = static_cast<int>(std::floor(h->originX())), oz = static_cast<int>(std::floor(h->originZ()));
    auto centre = [&](int i, int j) { return std::pair<float, float>(ox + (i + 0.5f) * cs, oz + (j + 0.5f) * cs); };
    static const int NI[4] = {1, -1, 0, 0}, NJ[4] = {0, 0, 1, -1};
    for (int j = b.bboxMin.y; j <= b.bboxMax.y; ++j)
        for (int i = b.bboxMin.x; i <= b.bboxMax.x; ++i) {
            const auto c = centre(i, j);
            if (wb->bodyIdAt(c.first, c.second) != b.id) continue;
            const float L = g.waterLevelAt(c.first, c.second);
            if (L <= HydrologyMap::NO_WATER * 0.5f) continue;   // dried under the corrected level
            for (int k = 0; k < 4; ++k) {
                const int ni = i + NI[k], nj = j + NJ[k];
                if (ni < 0 || nj < 0 || ni >= h->cellsX() || nj >= h->cellsZ()) continue;
                const auto nc = centre(ni, nj);
                if (g.waterLevelAt(nc.first, nc.second) > HydrologyMap::NO_WATER * 0.5f) continue;   // wet neighbour: not a rim
                const int x0 = ox + ni * cs, z0 = oz + nj * cs;
                for (int t = 0; t < cs; ++t) {
                    int x, z;
                    if (NI[k] == 1)       { x = x0;          z = z0 + t; }
                    else if (NI[k] == -1) { x = x0 + cs - 1; z = z0 + t; }
                    else if (NJ[k] == 1)  { x = x0 + t;      z = z0; }
                    else                  { x = x0 + t;      z = z0 + cs - 1; }
                    const int surf = g.sampleSurface(x, z).surfaceY;
                    ++rc.rim;
                    const int leak = static_cast<int>(std::floor(L)) - surf;
                    if (leak > 0) { ++rc.leaks; if (leak > rc.worst) { rc.worst = leak; rc.worstX = x; rc.worstZ = z; } }
                }
            }
        }
    return rc;
}

std::vector<const WaterBodyIndex::Body*> nearestAboveSeaBasins(const WorldGenerator& g, size_t count) {
    const HydrologyMap* h = g.hydrology();
    const WaterBodyIndex* wb = g.waterBodies();
    std::vector<const WaterBodyIndex::Body*> out;
    for (const auto& b : wb->bodies())
        if (b.cls != WaterBodyIndex::Class::Ocean && b.level > h->seaLevel() + 1.0f) out.push_back(&b);
    auto dist = [&](const WaterBodyIndex::Body* b) {
        const float cx = h->originX() + (b->bboxMin.x + b->bboxMax.x + 1) * 0.5f * h->cellSize();
        const float cz = h->originZ() + (b->bboxMin.y + b->bboxMax.y + 1) * 0.5f * h->cellSize();
        return cx * cx + cz * cz;
    };
    std::sort(out.begin(), out.end(), [&](auto* a, auto* b) { return dist(a) < dist(b); });
    if (out.size() > count) out.resize(count);
    return out;
}

}  // namespace

TEST(BasinRimTest, NoRimColumnBelowItsBasinLevel) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(v2Recipe(20260816u));
    ASSERT_TRUE(g.hydrology() && g.waterBodies());
    const auto basins = nearestAboveSeaBasins(g, 24);
    ASSERT_FALSE(basins.empty());
    long rim = 0, leaks = 0; int worst = 0; int lowered = 0, dried = 0;
    for (const auto* b : basins) {
        const RimCheck rc = checkBasin(g, *b);
        const float L = g.basinLevel(b->id);
        if (L < b->level - 1e-4f) ++lowered;
        if (rc.rim == 0) ++dried;
        rim += rc.rim; leaks += rc.leaks; worst = std::max(worst, rc.worst);
        if (rc.leaks > 0)
            std::printf("    basin %d (%d cells, spill %.2f -> level %.2f): rim %ld, leaks %ld, worst %d u at (%d,%d)\n",
                        b->id, b->areaCells, b->level, L, rc.rim, rc.leaks, rc.worst, rc.worstX, rc.worstZ);
    }
    const auto st = g.rimCorrector() ? g.rimCorrector()->stats() : BasinRimCorrector::Stats{};
    std::printf("[ M2c ] %zu nearest above-sea basins: rim columns %ld, leaks %ld, worst %d u; levels lowered %d, dried %d; corrector: %d computed, %ld ring columns sampled, max %d iterations, max drop %.1f u\n",
                basins.size(), rim, leaks, worst, lowered, dried, st.bodiesCorrected, st.ringColumns, st.maxIterations, st.maxDrop);
    EXPECT_EQ(leaks, 0) << "a rim column sits below the level it borders: the water would leave its basin";
}

// Every copy of a generator (the streaming workers) and the generator itself serve the same
// corrected level — the correction is shared state keyed by the bake, not per-copy work.
TEST(BasinRimTest, CorrectedLevelsAgreeAcrossGeneratorCopies) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(v2Recipe(20260816u));
    ASSERT_TRUE(g.waterBodies());
    WorldGenerator copy = g;
    const auto basins = nearestAboveSeaBasins(g, 12);
    ASSERT_FALSE(basins.empty());
    for (const auto* b : basins) {
        const float a = g.basinLevel(b->id), c = copy.basinLevel(b->id);
        EXPECT_FLOAT_EQ(a, c) << "basin " << b->id;
        EXPECT_LE(a, b->level + 1e-4f) << "a corrected level can only fall";
    }
}
