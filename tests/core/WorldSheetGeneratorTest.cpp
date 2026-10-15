#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <vector>

#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"

using namespace Phyxel;

// ============================================================================
// WorldSheetGeneratorTest: M1b (docs/WorldForgeV2Plan.md section 5 M1, D10). A version-2 recipe
// serves Layer-0 from the macro/meso sheet; a version-1 recipe never touches it. The worker-copy
// contract (RoadFieldSeamTest.GeneratorCopyStampsIdenticalRoadColumns shape) holds across a
// meso tile seam, and the meso tile bake time is recorded for the section 9 budget.
// ============================================================================
namespace {

WorldRecipe v2Recipe(uint32_t seed) {
    WorldRecipe r;
    r.version = 2;
    r.type = "Perlin";
    r.seed = seed;
    return r;
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

}  // namespace

TEST(WorldSheetGeneratorTest, VersionTwoRecipeEnablesTheSheetAndVersionOneDoesNot) {
    WorldGenerator v1(WorldGenerator::GenerationType::Perlin, 4242u);
    EXPECT_FALSE(v1.sheetMode());
    EXPECT_EQ(v1.mesoPager(), nullptr);
    WorldRecipe legacy;
    legacy.seed = 4242u;
    v1.applyRecipe(legacy);
    EXPECT_FALSE(v1.sheetMode()) << "a version-1 recipe enabled the sheet (D10)";
    EXPECT_EQ(v1.macroSheet(), nullptr);

    WorldGenerator v2(WorldGenerator::GenerationType::Perlin, 4242u);
    v2.applyRecipe(v2Recipe(4242u));
    ASSERT_TRUE(v2.sheetMode());
    ASSERT_NE(v2.macroSheet(), nullptr);
    ASSERT_NE(v2.mesoPager(), nullptr);
    EXPECT_EQ(v2.macroSheet()->cells(), 200);
    EXPECT_FLOAT_EQ(v2.macroSheet()->cellU(), 512.0f);
    EXPECT_EQ(v2.sheetParams().apronCells(), 4 * kMesoStencilRadius + kMesoBicubicReach);
}

// The worker-copy contract across a meso tile seam: the original and a copy (the streaming
// worker's generator is a default copy) sample 120 columns straddling x = 8192 in opposite
// orders and agree bit for bit on every served field.
TEST(WorldSheetGeneratorTest, GeneratorCopySamplesIdenticalColumnsAcrossATileSeam) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 4242u);
    gen.applyRecipe(v2Recipe(4242u));
    ASSERT_TRUE(gen.sheetMode());
    WorldGenerator copy = gen;   // what ChunkManager hands its workers
    EXPECT_EQ(copy.mesoPager(), gen.mesoPager()) << "the copy must share the pager, not own one";

    const float seamX = gen.sheetParams().tileU();   // 8192
    const int x0 = static_cast<int>(seamX) - 60, x1 = static_cast<int>(seamX) + 60;
    const int wz = 100;
    std::vector<WorldGenerator::ColumnSample> a, b;
    for (int x = x0; x < x1; ++x) a.push_back(gen.sampleSurface(x, wz));
    for (int x = x1 - 1; x >= x0; --x) b.push_back(copy.sampleSurface(x, wz));
    ASSERT_EQ(a.size(), b.size());
    size_t mismatches = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto& ca = a[i];
        const auto& cb = b[b.size() - 1 - i];
        const bool same = ca.surfaceY == cb.surfaceY && sameBits(ca.bedHeight, cb.bedHeight) &&
                          sameBits(ca.temperature, cb.temperature) && sameBits(ca.moisture, cb.moisture) &&
                          sameBits(ca.continentalness, cb.continentalness) && ca.surfaceMat == cb.surfaceMat &&
                          ca.biomeIndex == cb.biomeIndex;
        if (!same) ++mismatches;
    }
    EXPECT_EQ(mismatches, 0u) << mismatches << " of " << a.size()
                              << " columns differ between a generator and its copy across the seam";
    // The seam itself is continuous in the served height: no step larger than the fine band's
    // own roughness between the two columns either side of x = 8192.
    const auto& left = a[static_cast<size_t>(x1 - x0) / 2 - 1];
    const auto& right = a[static_cast<size_t>(x1 - x0) / 2];
    EXPECT_LT(std::abs(left.bedHeight - right.bedHeight), 8.0f)
        << "bedHeight steps " << left.bedHeight << " -> " << right.bedHeight << " across the tile seam";
}

// The hydrology bake and the columns flood the SAME surface: baseHeightFn() equals the column's
// pre-valley, pre-carve height where the biome blend is identity-free (blendScale 1, offset 0 are
// not guaranteed, so compare against the sheet terms directly).
TEST(WorldSheetGeneratorTest, BaseHeightFnReadsTheSheet) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 4242u);
    gen.applyRecipe(v2Recipe(4242u));
    auto fn = gen.baseHeightFn();
    auto pager = gen.mesoPager();
    ASSERT_NE(pager, nullptr);
    int checked = 0;
    for (int x = -3000; x <= 3000; x += 377)
        for (int z = -3000; z <= 3000; z += 533) {
            const float h = fn(static_cast<float>(x), static_cast<float>(z));
            const float meso = pager->heightAt(static_cast<float>(x), static_cast<float>(z));
            // fine band is >= 0 and bounded by kHillFineAmp (22) for Perlin
            EXPECT_GE(h, meso - 1e-3f) << "at " << x << "," << z;
            EXPECT_LE(h, meso + 22.0f + 1e-3f) << "at " << x << "," << z;
            ++checked;
        }
    EXPECT_GT(checked, 100);
}

// A type change after the recipe must rebuild the sheet (auditor finding 2026-10-15: the setter
// only cleared the column cache, so the pager stayed keyed to the old type).
TEST(WorldSheetGeneratorTest, SetGenerationTypeAfterRecipeRebuildsTheSheet) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 4242u);
    gen.applyRecipe(v2Recipe(4242u));
    ASSERT_TRUE(gen.sheetMode());
    const auto perlinPager = gen.mesoPager();
    const uint64_t perlinHash = gen.macroSheet()->hash();
    const float hPerlin = perlinPager->heightAt(1000.0f, 1000.0f);
    gen.setGenerationType(WorldGenerator::GenerationType::Mountains);
    ASSERT_NE(gen.mesoPager(), nullptr);
    EXPECT_NE(gen.mesoPager(), perlinPager) << "the pager still serves the Perlin bake";
    EXPECT_NE(gen.macroSheet()->hash(), perlinHash) << "the macro sheet is still the Perlin bake";
    const float hMtn = gen.mesoPager()->heightAt(1000.0f, 1000.0f);
    EXPECT_NE(hPerlin, hMtn) << "Mountains relief did not reach the tile";
}

// Section 9: meso tile bake time, recorded (Debug here; the Release figure is the gate's).
TEST(WorldSheetGeneratorTest, MesoTileBakeTimeRecorded) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 4242u);
    gen.applyRecipe(v2Recipe(4242u));
    auto pager = gen.mesoPager();
    ASSERT_NE(pager, nullptr);
    const uint64_t before = pager->bakes();
    const auto t0 = std::chrono::steady_clock::now();
    (void)pager->get(5, 5);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    EXPECT_EQ(pager->bakes(), before + 1);
    std::printf("[ M1 §9 ] meso tile 256x256 (+apron %d) bake: %.1f ms (this build config)\n",
                gen.sheetParams().apronCells(), ms);
    EXPECT_LT(ms, 20000.0) << "a tile bake over 20 s is unusable even in Debug";
}
