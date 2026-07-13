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

constexpr int   kSeaLevel  = 16;
constexpr float kLapse     = (6.5f / 1000.0f) * 15.0f / 35.0f;  // mirrors kLapse01PerVoxel
constexpr float kSnow01    = 5.0f / 35.0f;                      // mirrors kSnowTemp01  (0 °C)
constexpr float kTreeline01 = -3.0f / 35.0f;                    // mirrors kTreelineTemp01 (−8 °C)

// SnowGrass (snow-dusted taiga ground) is a snowy surface, NOT the pre-P1 bare-grass-on-peaks bug.
bool isGrass(const std::string& m) {
    return m == "Grass" || m == "GrassForest" || m == "GrassSavanna";
}

TEST(TerrainMaterialTest, SnowRockSeabedOnRealTerrain) {
    WorldGenerator mtns(WorldGenerator::GenerationType::Mountains, 4242u);

    long high = 0, highGrass = 0;         // tall columns, and how many are (wrongly) bare grass
    long snowAtAltitude = 0;              // bare permanent snow above the treeline
    long stoneSurface = 0;                // exposed-rock faces
    long warmLowSnow = 0;                 // BAD: any snow surface on a warm lowland
    for (int z = -256; z <= 256; z += 2)
        for (int x = -256; x <= 256; x += 2) {
            auto cs = mtns.sampleSurface(x, z);
            int alt = cs.surfaceY - kSeaLevel;
            const bool snowy = (cs.surfaceMat == "Snow" || cs.surfaceMat == "SnowGrass");
            if (cs.surfaceMat == "Stone") ++stoneSurface;
            if (cs.surfaceMat == "Snow" && alt > 150) ++snowAtAltitude;
            if (cs.temperature > 0.55f && alt < 20 && snowy) ++warmLowSnow;
            if (alt > 250) { ++high; if (isGrass(cs.surfaceMat)) ++highGrass; }
        }
    std::printf("[mat] highPeaks=%ld highGrass=%ld  snowAlt=%ld stoneSurf=%ld warmLowSnow=%ld\n",
                high, highGrass, snowAtAltitude, stoneSurface, warmLowSnow);

    // Tall peaks are snow/rock, essentially never bare grass — impossible pre-P1 (temperate peaks
    // were grass then). Allow a tiny fraction for very hot, gentle high ground.
    ASSERT_GT(high, 50) << "sample region has too few tall peaks to judge";
    EXPECT_LT(static_cast<double>(highGrass) / high, 0.02)
        << "tall peaks are grassy — snow/rock surfacing not applied (pre-P1 behavior)";
    // Bare permanent snow actually forms above the treeline, and rock faces exist.
    EXPECT_GT(snowAtAltitude, 0) << "no snow line formed";
    EXPECT_GT(stoneSurface, 0) << "no exposed rock on steep faces";
    // Guardrail: the lapse model must NOT snow warm lowlands (neither bare Snow nor SnowGrass).
    EXPECT_EQ(warmLowSnow, 0) << "snow bled onto warm low ground — lapse/threshold wrong";
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

TEST(TerrainMaterialTest, SnowOnlyWhereEffectiveTempIsFreezing) {
    // Falsifiability for the lapse-rate override: every BARE-snow column (surfaceMat "Snow") must sit
    // below the alpine-treeline temperature. If bare snow appears where effTemp is warm, the model is
    // decorative, not physical. (Steep cold peaks may be Stone instead — that's fine.)
    // NOTE — scope: this scans BARE "Snow" ONLY. SnowGrass is deliberately NOT temperature-bounded here:
    // it arises both from this override (below the snow line) AND as the Snow biome's OWN base surface,
    // which can sit slightly above freezing — the two are indistinguishable in sampleSurface output, so a
    // blanket SnowGrass upper-temp assertion would false-positive on legitimate biome-base taiga. That
    // SnowGrass appears only in the correct band is covered instead by the branch order (Snow, the colder
    // band, is checked first) plus the flora-gate test (onTaiga>0, onSnow==0).
    WorldGenerator mtns(WorldGenerator::GenerationType::Mountains, 7u);
    long bareSnow = 0, snowViolations = 0;
    for (int z = -200; z <= 200; z += 2)
        for (int x = -200; x <= 200; x += 2) {
            auto cs = mtns.sampleSurface(x, z);
            if (cs.surfaceMat != "Snow") continue;
            ++bareSnow;
            float effTemp = cs.temperature - (cs.surfaceY - kSeaLevel) * kLapse;
            if (effTemp >= kTreeline01 + 1e-3f) ++snowViolations;  // bare snow must be above the treeline
        }
    std::printf("[mat] bareSnow=%ld snowViolations=%ld\n", bareSnow, snowViolations);
    ASSERT_GT(bareSnow, 0);
    EXPECT_EQ(snowViolations, 0) << "bare snow placed below the alpine treeline (too warm)";
}

}  // namespace
}  // namespace Phyxel
