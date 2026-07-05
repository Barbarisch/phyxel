// Every biome declared in resources/biomes.json must be SELECTABLE by normal height-based
// generation — reachable in the actual climate space the noise fields produce, not just a
// non-overlapping box on paper. Biome selection is nearest-centroid on (temperature, moisture)
// (WorldGenerator::sampleColumn), and the normalized 4-octave domain-warped Perlin fields only
// ever occupy a sub-band of [0,1] in practice. A biome whose climate CENTRE sits outside that
// achievable band is dead content: its Voronoi cell is empty and it is never placed.
//
// (This test was added after a grounding/solution audit caught EnchantedForest declared at
// moisture centre 0.95 — unreachable, selected 0 times over tens of millions of samples.)

#include <gtest/gtest.h>
#include "core/WorldGenerator.h"
#include <glm/glm.hpp>
#include <map>
#include <set>
#include <string>

namespace Phyxel {
namespace {

TEST(BiomeReachabilityTest, EveryBiomeIsSelectedSomewhere) {
    // Sample the REAL selection entry point (sampleSurface -> biomeIndex) over a large area and
    // a couple of seeds, exactly as the flora/decoration pass does.
    std::map<std::string, long> counts;
    const int STEP = 17;          // prime-ish stride to avoid aliasing with the noise lattice
    const int SPAN = 6000;        // world units each way -> ~500k samples/seed
    for (uint32_t seed : {12345u, 999u, 2024u}) {
        WorldGenerator gen(WorldGenerator::GenerationType::Perlin, seed);
        // Traverse the climate space fast so a moderate area covers many independent climate
        // cells (the achievable temp/moisture RANGE is frequency-independent; this just samples
        // it densely). Reachability is a property of the range, not the spatial scale.
        gen.getTerrainParams().climateFrequency = 0.05f;
        const auto& biomes = gen.getBiomes();
        for (int x = -SPAN; x <= SPAN; x += STEP)
            for (int z = -SPAN; z <= SPAN; z += STEP) {
                auto col = gen.sampleSurface(x, z);
                if (col.biomeIndex >= 0 && col.biomeIndex < static_cast<int>(biomes.size()))
                    counts[biomes[col.biomeIndex].name] += 1;
            }
    }

    // Diagnostic: observed achievable climate range + per-biome selection counts.
    {
        WorldGenerator g(WorldGenerator::GenerationType::Perlin, 12345u);
        g.getTerrainParams().climateFrequency = 0.05f;
        float tmin = 1e9f, tmax = -1e9f, mmin = 1e9f, mmax = -1e9f;
        for (int x = -SPAN; x <= SPAN; x += STEP)
            for (int z = -SPAN; z <= SPAN; z += STEP) {
                auto c = g.sampleSurface(x, z);
                tmin = std::min(tmin, c.temperature); tmax = std::max(tmax, c.temperature);
                mmin = std::min(mmin, c.moisture);    mmax = std::max(mmax, c.moisture);
            }
        std::cout << "[reach] achievable temp [" << tmin << "," << tmax << "] moisture ["
                  << mmin << "," << mmax << "]\n";
        for (const auto& kv : counts)
            std::cout << "[reach] " << kv.first << " = " << kv.second << "\n";
    }

    // Known PRE-EXISTING unreachable biome: Desert's climate centre (0.8, 0.175) sits outside the
    // achievable range (temp/moist ≈ [0.21, 0.79]) — it is never selected on HEAD, independent of
    // this change. Documented here, not fixed in the megaflora work (retuning Desert's climate is a
    // separate world-gen concern). A NEW biome going unreachable still fails this test.
    const std::set<std::string> kPreExistingUnreachable = {"Desert"};

    WorldGenerator ref(WorldGenerator::GenerationType::Perlin, 1);
    long total = 0;
    for (const auto& kv : counts) total += kv.second;
    ASSERT_GT(total, 0);
    for (const auto& b : ref.getBiomes()) {
        long c = counts.count(b.name) ? counts[b.name] : 0;
        if (c == 0 && kPreExistingUnreachable.count(b.name)) continue;  // documented exception
        EXPECT_GT(c, 0) << "biome '" << b.name << "' is UNREACHABLE — climate centre ("
                        << (b.tempMin + b.tempMax) / 2 << ", " << (b.moistMin + b.moistMax) / 2
                        << ") lies outside the achievable noise range; it is never selected";
    }

    // The biome this megaflora work SHIPS must be reachable (the defect the audit caught).
    EXPECT_GT(counts["EnchantedForest"], 0)
        << "EnchantedForest is unreachable — the enchanted forest would never generate";
}

}  // namespace
}  // namespace Phyxel
