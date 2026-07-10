#include <gtest/gtest.h>

#include "core/WorldGenerator.h"

#include <cstdio>
#include <string>

// L2 validation for P1 slope/altitude/temperature material rules (docs/TerrainGenerationV2.md §P1).
// Asserts physical surfacing on the REAL generator output: a lapse-rate snow line, exposed rock on
// steep faces, and a sand/gravel seabed below sea level. RED baseline: pre-P1 the surface was always
// the biome material — warm-climate peaks were grass, and below-sea columns were grass/biome — so
// these assertions fail on P0. Mirrors the impl constants (kept in sync by comment).

namespace Phyxel {
namespace {

constexpr int   kSeaLevel = 16;
constexpr float kLapse    = (6.5f / 1000.0f) * 15.0f / 35.0f;  // mirrors kLapse01PerVoxel
constexpr float kSnow01   = 5.0f / 35.0f;                      // mirrors kSnowTemp01

bool isGrass(const std::string& m) {
    return m == "Grass" || m == "GrassForest" || m == "GrassSavanna";
}

TEST(TerrainMaterialTest, SnowRockSeabedOnRealTerrain) {
    WorldGenerator mtns(WorldGenerator::GenerationType::Mountains, 4242u);

    long high = 0, highGrass = 0;         // tall columns, and how many are (wrongly) grassy
    long iceAtAltitude = 0;               // snow above the treeline
    long stoneSurface = 0;                // exposed-rock faces
    long warmLowIce = 0;                  // BAD: snow on a warm lowland
    for (int z = -256; z <= 256; z += 2)
        for (int x = -256; x <= 256; x += 2) {
            auto cs = mtns.sampleSurface(x, z);
            int alt = cs.surfaceY - kSeaLevel;
            if (cs.surfaceMat == "Stone") ++stoneSurface;
            if (cs.surfaceMat == "Ice" && alt > 150) ++iceAtAltitude;
            if (cs.temperature > 0.55f && alt < 20 && cs.surfaceMat == "Ice") ++warmLowIce;
            if (alt > 250) { ++high; if (isGrass(cs.surfaceMat)) ++highGrass; }
        }
    std::printf("[mat] highPeaks=%ld highGrass=%ld  iceAlt=%ld stoneSurf=%ld warmLowIce=%ld\n",
                high, highGrass, iceAtAltitude, stoneSurface, warmLowIce);

    // Tall peaks are snow/rock, essentially never grass — impossible pre-P1 (temperate peaks were
    // grass then). Allow a tiny fraction for very hot, gentle high ground.
    ASSERT_GT(high, 50) << "sample region has too few tall peaks to judge";
    EXPECT_LT(static_cast<double>(highGrass) / high, 0.02)
        << "tall peaks are grassy — snow/rock surfacing not applied (pre-P1 behavior)";
    // Snow actually appears at altitude, and rock faces exist.
    EXPECT_GT(iceAtAltitude, 0) << "no snow line formed";
    EXPECT_GT(stoneSurface, 0) << "no exposed rock on steep faces";
    // Guardrail: the lapse model must NOT snow warm lowlands.
    EXPECT_EQ(warmLowIce, 0) << "snow bled onto warm low ground — lapse/threshold wrong";
}

TEST(TerrainMaterialTest, SeabedBelowSeaLevelIsSand) {
    // Every below-sea-level column is sand seabed, never grass/biome soil (RED on pre-P1). We assert
    // Sand SPECIFICALLY (not "Sand or Gravel") — at P1's shallow ocean depth there is no reachable
    // deeper-sediment band, so a two-material test would pass vacuously. Shelf/abyssal zonation is P2.
    WorldGenerator perlin(WorldGenerator::GenerationType::Perlin, 4242u);
    // Continental-scale strided scan: continents are ~6.7 km, so ocean basins only appear across a
    // continent-spanning area (a small window at origin sits in a continent interior). Stride keeps it
    // affordable; every below-sea column sampled is still checked individually.
    long belowSea = 0, notSand = 0;
    for (int z = -8192; z <= 8192; z += 24)
        for (int x = -8192; x <= 8192; x += 24) {
            auto cs = perlin.sampleSurface(x, z);
            if (cs.surfaceY < kSeaLevel) {
                ++belowSea;
                if (cs.surfaceMat != "Sand") ++notSand;
            }
        }
    std::printf("[mat] belowSea=%ld notSand=%ld\n", belowSea, notSand);
    ASSERT_GT(belowSea, 0) << "no ocean basins in the sample region to test seabed";
    EXPECT_EQ(notSand, 0) << "non-sand material found below sea level — seabed rule wrong";
}

TEST(TerrainMaterialTest, IceOnlyWhereEffectiveTempIsFreezing) {
    // Falsifiability: every snow column must satisfy the lapse-rate freezing condition
    // (temperature - altitude*lapse < freezing). If snow appears where effTemp is warm, the
    // model is decorative, not physical. (Steep cold peaks may be Stone instead — that's fine.)
    WorldGenerator mtns(WorldGenerator::GenerationType::Mountains, 7u);
    long ice = 0, violations = 0;
    for (int z = -200; z <= 200; z += 2)
        for (int x = -200; x <= 200; x += 2) {
            auto cs = mtns.sampleSurface(x, z);
            if (cs.surfaceMat != "Ice") continue;
            ++ice;
            float effTemp = cs.temperature - (cs.surfaceY - kSeaLevel) * kLapse;
            if (effTemp >= kSnow01 + 1e-3f) ++violations;
        }
    std::printf("[mat] iceColumns=%ld effTempViolations=%ld\n", ice, violations);
    ASSERT_GT(ice, 0);
    EXPECT_EQ(violations, 0) << "snow placed where effective temperature is above freezing";
}

}  // namespace
}  // namespace Phyxel
