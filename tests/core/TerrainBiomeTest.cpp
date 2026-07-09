#include <gtest/gtest.h>

#include "core/WorldGenerator.h"

#include <cstdio>
#include <map>
#include <string>

// L2 validation for P1 increment 2 (docs/TerrainGenerationV2.md §P1): the new land biomes
// (Jungle, Tundra) are actually SELECTED in their grounded climate niches, the pre-existing biomes
// are NOT crowded out (no regression), and the continentalness axis addition didn't change which
// biome wins for full-range biomes. Drives the real WorldGenerator biome selection.

namespace Phyxel {
namespace {

int biomeIndexByName(const WorldGenerator& g, const std::string& name) {
    const auto& bs = g.getBiomes();
    for (size_t i = 0; i < bs.size(); ++i)
        if (bs[i].name == name) return static_cast<int>(i);
    return -1;
}

TEST(TerrainBiomeTest, NewBiomesSelectedInTheirNiches) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 2026u);  // ctor loads resources/biomes.json
    // This test is meaningful only against the shipped biomes.json (which defines Jungle+Tundra),
    // not the 5 built-in defaults. Fail loudly if it didn't load — that's a real problem, not a skip.
    ASSERT_GE(biomeIndexByName(g, "Jungle"), 0) << "biomes.json not loaded (Jungle missing)";
    ASSERT_GE(biomeIndexByName(g, "Tundra"), 0) << "biomes.json not loaded (Tundra missing)";

    // Climate is very low frequency (~500-unit wavelength); sweep several wavelengths so the full
    // temperature×moisture space is covered and every niche is reachable.
    std::map<std::string, long> counts;
    double jT = 0, jM = 0, tT = 0, tM = 0; long jn = 0, tn = 0;
    for (int z = -1800; z <= 1800; z += 12)
        for (int x = -1800; x <= 1800; x += 12) {
            auto cs = g.sampleSurface(x, z);
            const std::string& name = g.getBiomes()[cs.biomeIndex].name;
            ++counts[name];
            if (name == "Jungle") { jT += cs.temperature; jM += cs.moisture; ++jn; }
            if (name == "Tundra") { tT += cs.temperature; tM += cs.moisture; ++tn; }
        }
    std::printf("[biome] Jungle=%ld Tundra=%ld | Forest=%ld Plains=%ld Desert=%ld Savanna=%ld Snow=%ld\n",
                counts["Jungle"], counts["Tundra"], counts["Forest"], counts["Plains"],
                counts["Desert"], counts["Savanna"], counts["Snow"]);
    if (jn) std::printf("[biome] Jungle avg temp=%.2f moist=%.2f\n", jT / jn, jM / jn);
    if (tn) std::printf("[biome] Tundra avg temp=%.2f moist=%.2f\n", tT / tn, tM / tn);

    // Both new biomes are actually reachable (appear in the map) — RED before the biomes.json add.
    EXPECT_GT(counts["Jungle"], 0) << "Jungle never selected — niche unreachable";
    EXPECT_GT(counts["Tundra"], 0) << "Tundra never selected — niche unreachable";
    // They land in the RIGHT niche: jungle hot+wet, tundra cold+dry (not just anywhere).
    if (jn) { EXPECT_GT(jT / jn, 0.7) << "Jungle not hot"; EXPECT_GT(jM / jn, 0.6) << "Jungle not wet"; }
    if (tn) { EXPECT_LT(tT / tn, 0.25) << "Tundra not cold"; EXPECT_LT(tM / tn, 0.30) << "Tundra not dry"; }
    // No regression: the core pre-existing biomes still occupy meaningful area.
    EXPECT_GT(counts["Forest"], 0) << "Forest crowded out";
    EXPECT_GT(counts["Plains"], 0) << "Plains crowded out";
    EXPECT_GT(counts["Desert"], 0) << "Desert crowded out";
}

TEST(TerrainBiomeTest, FloraDoesNotLandOnCliffsSnowOrSeabed) {
    // The flora gate: planFlora must not place plants where sampleColumn surfaced the ground as a
    // bare-rock cliff (Stone), a snow cap on a non-snow biome (Ice override), or the seabed (below
    // sea level). Verifies the invariant on planFlora's real output; RED before the gate was added.
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 4242u);  // has cliffs + snow + basins
    auto placements = g.planFlora(-300, -300, 300, 300, 8);
    ASSERT_GT(placements.size(), 100u) << "no flora produced to check";

    long onSeabed = 0, onCliff = 0, onSnow = 0;
    for (const auto& p : placements) {
        auto cs = g.sampleSurface(p.worldX, p.worldZ);
        if (cs.surfaceY < 16) ++onSeabed;
        if (cs.surfaceMat == "Stone") ++onCliff;
        if (cs.surfaceMat == "Ice" && g.getBiomes()[cs.biomeIndex].surfaceMaterial != "Ice") ++onSnow;
    }
    std::printf("[flora] placements=%zu onSeabed=%ld onCliff=%ld onSnow=%ld\n",
                placements.size(), onSeabed, onCliff, onSnow);
    EXPECT_EQ(onSeabed, 0) << "plants on the seabed";
    EXPECT_EQ(onCliff, 0) << "plants on bare-rock cliffs";
    EXPECT_EQ(onSnow, 0) << "plants on snowcaps (non-snow biome)";
}

}  // namespace
}  // namespace Phyxel
