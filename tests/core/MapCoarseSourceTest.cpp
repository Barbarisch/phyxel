#include <gtest/gtest.h>

#include <memory>

#include "core/MapCoarseSource.h"
#include "core/WorldGenerator.h"

using namespace Phyxel;

namespace {
// A 4x4 map with a strong increasing gradient, one pixel per `bpp` world blocks.
// value(px,pz) = 16 + 40*(px + pz)  → world Y from 16 (corner) up to 256.
std::shared_ptr<MapCoarseData> makeGradientMap(float bpp) {
    auto d = std::make_shared<MapCoarseData>();
    d->widthPx = d->heightPx = 4;
    d->blocksPerPixel = bpp;
    d->worldSizeBlocks = 4 * bpp;
    d->seaLevelY = 16.0f;
    d->height.resize(16);
    for (int z = 0; z < 4; ++z)
        for (int x = 0; x < 4; ++x)
            d->height[z * 4 + x] = static_cast<uint16_t>(16 + 40 * (x + z));
    d->minY = 16.0f; d->maxY = 256.0f;
    return d;
}
}  // namespace

TEST(MapCoarseSource, NearestPixelAndClamp) {
    auto d = makeGradientMap(4.0f);          // world 0..16, pixels at world 0,4,8,12
    EXPECT_FLOAT_EQ(d->heightAtPixelClamped(0, 0), 16.0f);   // pixel (0,0)
    EXPECT_FLOAT_EQ(d->heightAtPixelClamped(12, 12), 256.0f); // pixel (3,3) = 16+40*6
    // Outside the map clamps to the nearest edge pixel (no out-of-bounds).
    EXPECT_FLOAT_EQ(d->heightAtPixelClamped(-1000, -1000), 16.0f);
    EXPECT_FLOAT_EQ(d->heightAtPixelClamped(1e6f, 1e6f), 256.0f);
}

TEST(MapCoarseSource, BilinearMidpoint) {
    auto d = makeGradientMap(4.0f);
    // Halfway between pixel (0,0)=16 and (1,0)=56 → 36.
    EXPECT_NEAR(d->sampleHeightWorld(2.0f, 0.0f), 36.0f, 1e-3f);
    // Centre of the (0,0)-(1,1) cell: mean of 16,56,56,96 = 56.
    EXPECT_NEAR(d->sampleHeightWorld(2.0f, 2.0f), 56.0f, 1e-3f);
}

TEST(MapCoarseSource, SourceFuncElevationAndContinentalness) {
    auto d = makeGradientMap(4.0f);
    auto src = makeMapCoarseSource(d);
    const CoarseSample lo = src(0, 0);       // corner, lowest
    const CoarseSample hi = src(12, 12);     // corner, highest
    EXPECT_FLOAT_EQ(lo.baseHeight, 16.0f);
    EXPECT_FLOAT_EQ(hi.baseHeight, 256.0f);
    // continentalness = (baseHeight - sea) / (maxY - sea), clamped to [0,1].
    EXPECT_NEAR(lo.continentalness, 0.0f, 1e-4f);
    EXPECT_NEAR(hi.continentalness, 1.0f, 1e-4f);
}

// The imported map actually drives WorldGenerator's surface: a column over a high map
// pixel produces a higher surface than one over a low pixel, and the surface tracks the
// map base (Layer-1 relief only ADDS, so surfaceY >= baseHeight - 1).
TEST(MapCoarseSource, DrivesWorldGeneratorSurface) {
    auto d = makeGradientMap(100.0f);        // widely spaced columns: world 0..400
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 1234);
    EXPECT_FALSE(gen.hasHeightmapSource());
    gen.setHeightmapSource(d);
    EXPECT_TRUE(gen.hasHeightmapSource());

    const auto lo = gen.sampleSurface(0, 0);        // map base 16
    const auto hi = gen.sampleSurface(300, 300);    // map base 256 (pixel 3,3)
    EXPECT_GT(hi.surfaceY, lo.surfaceY);            // map elevation ordering preserved
    EXPECT_GE(hi.surfaceY, 256 - 1);               // surface tracks map base (relief only adds)
    EXPECT_GE(lo.surfaceY, 16 - 1);
}
