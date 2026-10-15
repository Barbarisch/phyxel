#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/WorldGenerator.h"

using namespace Phyxel;

// ============================================================================
// LegacyTerrainGoldenTest: D10 (docs/WorldForgeV2Plan.md section 3.1). The version-1 analytic
// Layer-0 is FROZEN. This pins the bit patterns of 2000 scattered columns (surfaceY, bedHeight,
// temperature, moisture, continentalness, biomeIndex, surfaceMat) for Perlin and Mountains at
// seed 4242 as one FNV-1a hash each. Uses only the v1 API (ctor + sampleSurface) so the SAME file
// compiles at a pre-M1 commit: the expected hashes were produced at the parent of the M1 work
// (see the evidence file named below) and must hold on every commit after it.
//
// Red check: perturb any v1 constant (a noise offset, kHillFineAmp, ...) and this fails.
// ============================================================================
namespace {

uint64_t fnv1a(uint64_t h, const void* p, size_t n) {
    const unsigned char* b = static_cast<const unsigned char*>(p);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}

uint64_t goldenHash(WorldGenerator::GenerationType type) {
    WorldGenerator gen(type, 4242u);
    uint64_t h = 1469598103934665603ull;
    // 2000 columns on a scattered lattice spanning +-12 km (inside the hydrology box, across many
    // biomes and relief regimes); never on a chunk-aligned grid.
    for (int i = 0; i < 2000; ++i) {
        const int wx = ((i * 7919) % 24001) - 12000;
        const int wz = ((i * 104729) % 24007) - 12003;
        const WorldGenerator::ColumnSample c = gen.sampleSurface(wx, wz);
        h = fnv1a(h, &c.surfaceY, sizeof(c.surfaceY));
        h = fnv1a(h, &c.bedHeight, sizeof(c.bedHeight));
        h = fnv1a(h, &c.temperature, sizeof(c.temperature));
        h = fnv1a(h, &c.moisture, sizeof(c.moisture));
        h = fnv1a(h, &c.continentalness, sizeof(c.continentalness));
        h = fnv1a(h, &c.biomeIndex, sizeof(c.biomeIndex));
        h = fnv1a(h, c.surfaceMat.data(), c.surfaceMat.size());
    }
    return h;
}

// Filled from the first run at the pre-M1 parent (docs/evidence/worldforge_v2/m1b_legacy_golden.txt).
constexpr uint64_t kGoldenPerlin    = 11413304086647064455ull;
constexpr uint64_t kGoldenMountains = 5521812291101006622ull;

}  // namespace

TEST(LegacyTerrainGoldenTest, PerlinColumnsAreByteIdenticalToThePreM1Golden) {
    const uint64_t h = goldenHash(WorldGenerator::GenerationType::Perlin);
    std::printf("[ D10 golden ] Perlin 4242 x 2000 columns: %llu\n", static_cast<unsigned long long>(h));
    EXPECT_EQ(h, kGoldenPerlin) << "the version-1 Perlin terrain changed (D10 forbids it)";
}

TEST(LegacyTerrainGoldenTest, MountainsColumnsAreByteIdenticalToThePreM1Golden) {
    const uint64_t h = goldenHash(WorldGenerator::GenerationType::Mountains);
    std::printf("[ D10 golden ] Mountains 4242 x 2000 columns: %llu\n", static_cast<unsigned long long>(h));
    EXPECT_EQ(h, kGoldenMountains) << "the version-1 Mountains terrain changed (D10 forbids it)";
}
