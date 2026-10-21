#include <gtest/gtest.h>

#include "core/CoastField.h"
#include "core/HydrologyMap.h"
#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Phyxel;

// ============================================================================
// CoastTest — M2e-2 (docs/WorldForgeV2Plan.md M2e; D17: the coast SHAPE, no water). On a v2 world
// with an ocean, transects across the shoreline must show a beach: on the land side a face no
// steeper than the grounded beach-face slope out to the berm (CEM Part III Ch. 3 Example III-3-6:
// m0 = 0.05, B = 2 m; §10 F4f), on the sea side a nearshore bed that never rises seaward inside
// the active width (the Dean profile h = A·y^(2/3), CEM Table III-3-3). A column the scree rule
// marks as rock keeps its cliff (coastCliff) and is exempt. Red on the committed code: the coast
// is a hard grass-meets-sea edge with whatever slope the relief produced (the 2026-07-10 gap).
// ============================================================================
namespace {

WorldRecipe v2Recipe(uint32_t seed) {
    WorldRecipe r;
    r.version = 2;
    r.type = "Perlin";
    r.seed = seed;
    return r;
}

}  // namespace

TEST(CoastTest, BeachFaceIsGentleAndTheNearshoreBedNeverRisesSeaward) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(v2Recipe(20260816u));
    ASSERT_TRUE(g.hydrology());
    const CoastField* coast = g.coastField();
    ASSERT_TRUE(coast && coast->hasOcean()) << "the baseline world has no ocean cells in its box";
    const HydrologyMap* h = g.hydrology();
    const float sea = h->seaLevel();
    // Shoreline crossings along rows: walk x at a fixed z, find where the signed distance changes
    // sign, and take the transect perpendicular to the row (close enough at this scale).
    int transects = 0, cliffTransects = 0, steepFace = 0, seawardRises = 0, sandFace = 0, faceCols = 0;
    float worstFace = 0.0f, worstRise = 0.0f;
    const int maxTransects = 60;
    for (int j = 16; j < h->cellsZ() - 16 && transects < maxTransects; j += 24) {
        const float wz = h->originZ() + (j + 0.5f) * h->cellSize();
        float prev = coast->signedDistAt(h->originX() + 16.0f, wz);
        for (int i = 17; i < h->cellsX() - 16 && transects < maxTransects; ++i) {
            const float wx = h->originX() + (i + 0.5f) * h->cellSize();
            const float sd = coast->signedDistAt(wx, wz);
            if (!((prev > 0.0f) != (sd > 0.0f)) || std::fabs(sd) > 1e29f) { prev = sd; continue; }
            // a crossing between cells i-1 and i: locate it to the column
            float x0 = h->originX() + (i - 0.5f) * h->cellSize(), x1 = wx;
            for (int it = 0; it < 12; ++it) {
                const float xm = 0.5f * (x0 + x1);
                if ((coast->signedDistAt(xm, wz) > 0.0f) == (prev > 0.0f)) x0 = xm; else x1 = xm;
            }
            const float shoreX = 0.5f * (x0 + x1);
            const float landDir = (prev > 0.0f) ? -1.0f : 1.0f;   // which way along x is land
            ++transects;
            prev = sd;
            // the face: land side, d = 1 .. 40 u in 1 u steps
            bool cliff = false; float maxSlope = 0.0f; int sand = 0, cols = 0;
            float lastBed = 0.0f; bool lastChannel = true;
            for (int d = 1; d <= 40; ++d) {
                const int cx = static_cast<int>(std::floor(shoreX + landDir * d));
                const auto c = g.sampleSurface(cx, static_cast<int>(std::floor(wz)));
                if (c.coastCliff) { cliff = true; break; }
                // a creek or river crossing the beach cuts its own channel across the face: the
                // step down into it and up its far bank are the channel's, not the face's
                const bool channel = c.riverOrder > 0 || c.channelDepth > 0.0f;
                // the FACE is defined by the field's signed distance (<= 40 u = B / m0), not by the
                // transect's own step count: a transect oblique to the shore, and the chamfer field's
                // cell-scale quantization, put the 40 u mark short of d = 40
                const float sdHere = coast->signedDistAt(static_cast<float>(cx) + 0.5f, wz + 0.5f);
                if (sdHere > 40.0f) break;
                if (d > 1 && !channel && !lastChannel) {
                    const float rise = c.bedHeight - lastBed;
                    if (rise > maxSlope) maxSlope = rise;
                    if (rise > 0.05f + 0.34f)
                        std::printf("    steep face step %.2f u at d=%d column (%d,%d): bed %.2f -> %.2f; coastBand %d cliff %d shelf %d road %d creekBed %d mat %s; sd %.1f\n",
                                    rise, d, cx, static_cast<int>(std::floor(wz)), lastBed, c.bedHeight, c.coastBand ? 1 : 0, c.coastCliff ? 1 : 0,
                                    c.bedShelf ? 1 : 0, c.roadClass, c.creekBed ? 1 : 0, c.surfaceMat.c_str(),
                                    coast->signedDistAt(static_cast<float>(cx) + 0.5f, wz + 0.5f));
                }
                lastBed = c.bedHeight; lastChannel = channel;
                if (c.surfaceMat == "Sand") ++sand;
                ++cols;
            }
            if (cliff) { ++cliffTransects; continue; }
            faceCols += cols; sandFace += sand;
            if (maxSlope > 0.05f + 0.34f) ++steepFace;   // the grounded face slope + one 1/3 u shelf step
            worstFace = std::max(worstFace, maxSlope);
            // the nearshore: sea side, d = 1 .. 120 u, the bed never rises seaward
            float prevBed = 1e30f; float rise = 0.0f;
            for (int d = 1; d <= 120; ++d) {
                const int cx = static_cast<int>(std::floor(shoreX - landDir * d));
                const auto c = g.sampleSurface(cx, static_cast<int>(std::floor(wz)));
                if (prevBed < 1e29f) rise = std::max(rise, c.bedHeight - prevBed);
                prevBed = c.bedHeight;
            }
            if (rise > 1.0f + 1e-3f) ++seawardRises;   // one voxel of relief noise is tolerated, a bar is not shaped
            worstRise = std::max(worstRise, rise);
        }
    }
    std::printf("[ M2e-2 ] %d shoreline transects (%d cliff coasts exempt): beach faces steeper than m0 + 1/3 u: %d, worst face rise per u %.2f; nearshore beds rising seaward by > 1 u: %d, worst rise %.2f u; Sand on %d of %d face columns; sea level %.1f, ocean cells %d\n",
                transects, cliffTransects, steepFace, worstFace, seawardRises, worstRise, sandFace, faceCols, sea, coast->seaCells());
    ASSERT_GT(transects, 0);
    EXPECT_EQ(steepFace, 0) << "a beach face steeper than the grounded slope";
    EXPECT_EQ(seawardRises, 0) << "the nearshore bed rises seaward";
    EXPECT_GT(sandFace, faceCols / 2) << "the beach is not sand";
}

// The field itself: signed, continuous across the shoreline, and tied to the bake's ocean bodies.
TEST(CoastTest, SignedDistanceCrossesZeroAtTheShoreline) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(v2Recipe(20260816u));
    const CoastField* coast = g.coastField();
    ASSERT_TRUE(coast && coast->hasOcean());
    const HydrologyMap* h = g.hydrology();
    const WaterBodyIndex* wb = g.waterBodies();
    int checked = 0, wrongSign = 0, seaCellsLandSide = 0, seaLevelCells = 0;
    for (int j = 0; j < h->cellsZ(); j += 8)
        for (int i = 0; i < h->cellsX(); i += 8) {
            const float wx = h->originX() + (i + 0.5f) * h->cellSize(), wz = h->originZ() + (j + 0.5f) * h->cellSize();
            const float sd = coast->signedDistAt(wx, wz);
            const float lvl = h->waterLevelAt(wx, wz);
            const bool wet = lvl > HydrologyMap::NO_WATER * 0.5f;
            const auto* b = wet ? wb->bodyAt(wx, wz) : nullptr;
            const bool seaLevelBody = b && std::fabs(b->level - h->seaLevel()) <= 1.0f && b->cls != WaterBodyIndex::Class::Pond;
            ++checked;
            // dry cells lie on the land side; every cell of a sea-level body that is not a pond —
            // the Ocean AND the baseline world's 114 km² sea-level inland sea — lies at sd <= 0
            if (!wet && sd <= 0.0f) ++wrongSign;
            if (seaLevelBody) { ++seaLevelCells; if (sd > 0.0f) ++seaCellsLandSide; }
        }
    std::printf("[ M2e-2 ] coast field: %d cells checked, %d dry cells with a sea-side sign, %d of %d sea-level-body cells on the land side\n",
                checked, wrongSign, seaCellsLandSide, seaLevelCells);
    EXPECT_EQ(wrongSign, 0);
    EXPECT_EQ(seaCellsLandSide, 0) << "a sea-level body's cells are not the sea for the coast field (its shores get no beach)";
}
