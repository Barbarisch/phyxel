#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/FlowField.h"
#include "core/HydrologyMap.h"
#include "core/WaterBodyIndex.h"
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

// Plan 13.1 "Flat / heightmap": a Flat version-2 world gets the sheet too (climate planes + a sea-level
// macro), and stays flat; a heightmap world keeps the version-1 path (the map IS its macro) — stated.
TEST(WorldSheetGeneratorTest, FlatVersionTwoWorldGetsATrivialSheetAndStaysFlat) {
    WorldGenerator flat(WorldGenerator::GenerationType::Flat, 4242u);
    WorldRecipe r = v2Recipe(4242u);
    r.type = "Flat";
    flat.applyRecipe(r);
    EXPECT_TRUE(flat.sheetMode());
    ASSERT_NE(flat.mesoPager(), nullptr) << "Flat has no sheet: WorldForge still has no plan for it";
    for (int x = -500; x <= 500; x += 125)
        EXPECT_EQ(flat.sampleSurface(x, 77).surfaceY, 16) << "Flat moved at x=" << x;
}

// Plan 13.1 "far mesher": every Layer-0 rebuild path moves the epoch a private copy compares to.
TEST(WorldSheetGeneratorTest, Layer0EpochMovesOnEveryRebuildPathAndCopiesCarryIt) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 4242u);
    const uint64_t e0 = gen.layer0Epoch();
    gen.applyRecipe(v2Recipe(4242u));
    const uint64_t e1 = gen.layer0Epoch();
    EXPECT_GT(e1, e0) << "applyRecipe did not move the epoch";
    WorldGenerator copy = gen;
    EXPECT_EQ(copy.layer0Epoch(), e1) << "a copy must carry the epoch it was taken at";
    gen.setGenerationType(WorldGenerator::GenerationType::Mountains);
    EXPECT_GT(gen.layer0Epoch(), e1) << "setGenerationType did not move the epoch";
    EXPECT_EQ(copy.layer0Epoch(), e1) << "the copy's epoch must not follow the live generator";
}

// D15: the sheet carries the macro drainage from the erosion bake's final routing, even with
// erosion off (routing only), so M2's meso tiles have their global boundary condition.
TEST(WorldSheetGeneratorTest, MacroSheetCarriesTheMacroDrainage) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 4242u);
    gen.applyRecipe(v2Recipe(4242u));   // erosionSteps 0: routing only
    auto macro = gen.macroSheet();
    ASSERT_NE(macro, nullptr);
    const int n = macro->cells();
    int baseLevel = 0, lakeCells = 0, cycles = 0;
    double baseArea = 0.0;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const MacroCell& c = macro->cell(i, j);
            ASSERT_GE(c.receiver, 0) << "cell (" << i << "," << j << ") has no receiver";
            if (c.receiver == j * n + i) { ++baseLevel; baseArea += c.drainageArea; }
            if (c.filled > c.baseHeight + 1e-3f) ++lakeCells;
            int cur = j * n + i, hops = 0;
            while (macro->cell(cur % n, cur / n).receiver != cur && hops <= n * n) { cur = macro->cell(cur % n, cur / n).receiver; ++hops; }
            if (hops > n * n) ++cycles;
        }
    EXPECT_GT(baseLevel, 0);
    EXPECT_EQ(cycles, 0) << "the macro drainage has a cycle";
    EXPECT_NEAR(baseArea, static_cast<double>(n) * n * 512.0 * 512.0, 1.0) << "drainage area is not conserved at base level";
    std::printf("[ M2 prereq ] macro drainage: %d base-level cells, %d lake cells (filled > base) of %d\n", baseLevel, lakeCells, n * n);
}

// The painted uplift mask raises the base where it is painted and nowhere else (M2's RiverLab
// needs a painted ridge and basin), and round-trips through the recipe JSON.
TEST(WorldSheetGeneratorTest, PaintedUpliftRaisesTheBaseWhereItIsPaintedAndRoundTrips) {
    WorldRecipe r = v2Recipe(4242u);
    SheetParams::UpliftBump bump;
    bump.x = 3000.0f; bump.z = -2000.0f; bump.radiusU = 1500.0f; bump.deltaU = 300.0f;
    r.sheet.upliftMask.push_back(bump);
    const WorldRecipe back = WorldRecipe::fromJson(r.toJson());
    ASSERT_EQ(back.sheet.upliftMask.size(), 1u);
    EXPECT_FLOAT_EQ(back.sheet.upliftMask[0].deltaU, 300.0f);
    EXPECT_TRUE(back.sheet.upliftMaskEquals(r.sheet));

    WorldGenerator plain(WorldGenerator::GenerationType::Perlin, 4242u);
    plain.applyRecipe(v2Recipe(4242u));
    WorldGenerator painted(WorldGenerator::GenerationType::Perlin, 4242u);
    painted.applyRecipe(r);
    EXPECT_NE(plain.macroSheet()->hash(), painted.macroSheet()->hash()) << "the mask is not in the sheet key";
    const float atCentre = painted.macroSheet()->sampleAt(3000.0f, -2000.0f).baseHeight - plain.macroSheet()->sampleAt(3000.0f, -2000.0f).baseHeight;
    const float farAway  = painted.macroSheet()->sampleAt(-30000.0f, 30000.0f).baseHeight - plain.macroSheet()->sampleAt(-30000.0f, 30000.0f).baseHeight;
    EXPECT_NEAR(atCentre, 300.0f, 30.0f) << "the bump did not raise its centre by its delta";
    EXPECT_NEAR(farAway, 0.0f, 1e-3f) << "the bump leaked 40 km away";
}

// ============================================================================
// M2b — the meso-resolution drainage bake (docs/WorldForgeV2Plan.md, M2 engineering design,
// 2026-10-21). A version-2 recipe bakes HydrologyMap/FlowField at the MESO cell (32 u) over the
// same ±16 384 u box the 128 u bake used; v1 keeps 128 u (D10). Red on the committed code: the
// bake is 128 u for every recipe.
// ============================================================================
TEST(WorldSheetGeneratorTest, V2BakesHydrologyAtTheMesoCell) {
    WorldGenerator v1(WorldGenerator::GenerationType::Perlin, 20260816u);
    WorldRecipe r1 = v2Recipe(20260816u);
    r1.version = 1;
    v1.applyRecipe(r1);
    // A seed no other fixture uses, so the process-wide hydro cache cannot have it: this applyRecipe
    // PAYS the bake, and its wall time is the number the plan's section 9 row records (per build config).
    WorldGenerator v2(WorldGenerator::GenerationType::Perlin, 777001u);
    const auto t0 = std::chrono::steady_clock::now();
    v2.applyRecipe(v2Recipe(777001u));
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    ASSERT_TRUE(v1.hydrology() && v1.riverNetwork() && v2.hydrology() && v2.riverNetwork());
    const float meso = v2.sheetParams().mesoCellU;
    EXPECT_FLOAT_EQ(v1.hydrology()->cellSize(), 128.0f) << "v1's bake cell moved (D10)";
    EXPECT_FLOAT_EQ(v2.hydrology()->cellSize(), meso) << "v2 does not bake at the meso cell";
    EXPECT_FLOAT_EQ(v2.riverNetwork()->cellSize(), meso);
    // Same box: origin and extent unchanged, so every consumer's region assumptions hold.
    EXPECT_FLOAT_EQ(v2.hydrology()->originX(), v1.hydrology()->originX());
    EXPECT_FLOAT_EQ(v2.hydrology()->cellsX() * v2.hydrology()->cellSize(),
                    v1.hydrology()->cellsX() * v1.hydrology()->cellSize());
    EXPECT_TRUE(v2.riverNetwork()->drainageComplete());
    std::printf("[ M2b ] v2 recipe bake incl. %dx%d-cell hydrology: %.0f ms (this build config); maxOrder %d, maxAccum %d\n",
                v2.hydrology()->cellsX(), v2.hydrology()->cellsZ(), ms, v2.riverNetwork()->maxOrder(),
                v2.riverNetwork()->maxAccum());
}

// D15's boundary condition: a macro river that enters the box from outside carries its macro
// drainage area in at the first interior cell nearest its crossing. Red on the committed code:
// the FlowField is given no inflows, so a river entering the box starts from nothing.
TEST(WorldSheetGeneratorTest, MacroInflowEntersTheBoxAtTheBorder) {
    WorldGenerator v2(WorldGenerator::GenerationType::Perlin, 20260816u);
    v2.applyRecipe(v2Recipe(20260816u));
    const FlowField* f = v2.riverNetwork();
    ASSERT_TRUE(f);
    const auto& in = f->inflows();
    ASSERT_FALSE(in.empty()) << "no macro inflow reached the bake: the boundary condition is not wired";
    long long injected = 0;
    int propagated = 0;
    for (const auto& x : in) {
        ASSERT_GE(x.cell, 0);
        ASSERT_LT(x.cell, f->cellsX() * f->cellsZ());
        const int i = x.cell % f->cellsX(), j = x.cell / f->cellsX();
        EXPECT_TRUE(i >= 1 && i <= f->cellsX() - 2 && j >= 1 && j <= f->cellsZ() - 2)
            << "an inflow on the border row is an outlet and drains straight back out";
        const float wx = f->originX() + (i + 0.5f) * f->cellSize(), wz = f->originZ() + (j + 0.5f) * f->cellSize();
        EXPECT_GE(f->accumAt(wx, wz), 1 + x.extraCells) << "the injected area is not in the cell's count";
        injected += x.extraCells;
        const glm::vec2 d = f->flowDirAt(wx, wz);
        if (d.x != 0.0f || d.y != 0.0f) {
            const int ni = i + static_cast<int>(std::lround(d.x)), nj = j + static_cast<int>(std::lround(d.y));
            const float nx = f->originX() + (ni + 0.5f) * f->cellSize(), nz = f->originZ() + (nj + 0.5f) * f->cellSize();
            EXPECT_GE(f->accumAt(nx, nz), 1 + x.extraCells) << "the injected area did not ride downstream";
            ++propagated;
        }
    }
    std::printf("[ M2b ] macro inflows: %zu entry cells, %.1f km2 injected, %d propagate downstream\n",
                in.size(), injected * f->cellSize() * f->cellSize() * 1e-6, propagated);
}

// The Lake/Pond split (infinite vs finite body) is an AREA: 4 cells at the 128 u reference cell is
// 65 536 m² (a lake); the same 4 cells at 32 u is 4 096 m² (a pond). Red on the committed code: the
// split counts cells, so the meso bake would turn every 6.5 ha lake into a scoopable pond.
TEST(WorldSheetGeneratorTest, LakeVsPondSplitIsAnAreaNotACellCount) {
    auto fixture = [](float cs) {
        // An 8x8 grid: a 2x2 basin at depth 10 in a 50 plateau; the border is the outlet, so the
        // basin fills to 50 and exactly its 4 cells are wet.
        HydrologyMap::HeightFunc h = [cs](float x, float z) {
            const int i = static_cast<int>(std::floor(x / cs)), j = static_cast<int>(std::floor(z / cs));
            return (i >= 3 && i <= 4 && j >= 3 && j <= 4) ? 10.0f : 50.0f;
        };
        HydrologyMap hydro(h, 0.0f, 0.0f, 8, 8, cs, /*seaLevel=*/0.0f);
        WaterBodyIndex bodies(hydro, h);
        return bodies.bodies();
    };
    const auto at128 = fixture(128.0f), at32 = fixture(32.0f);
    ASSERT_EQ(at128.size(), 1u);
    ASSERT_EQ(at32.size(), 1u);
    EXPECT_EQ(at128[0].areaCells, 4);
    EXPECT_EQ(at32[0].areaCells, 4);
    EXPECT_EQ(at128[0].cls, WaterBodyIndex::Class::Lake) << "4 cells at 128 u = 65 536 m2: a lake (the shipped meaning)";
    EXPECT_EQ(at32[0].cls, WaterBodyIndex::Class::Pond) << "4 cells at 32 u = 4 096 m2: a pond";
}

// ============================================================================
// M2b-2 — channel geometry from the grounded law (docs/WorldForgeV2Plan.md §10 F4f). On a v2
// bake every river cell's half-width and centreline depth are W = 2.37·A^0.36 / D = 0.296·A^0.30
// at its drainage area, and "carved river" means the centreline depth reaches 0.5 u. Red on the
// committed code: v2 still serves the per-order table (2/3/5/8/14/22 u, 0.33..2 u) and splits by
// order >= 3.
// ============================================================================
TEST(WorldSheetGeneratorTest, V2ChannelGeometryFollowsTheGroundedLaw) {
    WorldGenerator v2(WorldGenerator::GenerationType::Perlin, 20260816u);
    v2.applyRecipe(v2Recipe(20260816u));
    const FlowField* f = v2.riverNetwork();
    ASSERT_TRUE(f);
    EXPECT_TRUE(f->areaLaw()) << "a v2 bake does not serve the law";
    const float cs = f->cellSize();
    int river = 0, carved = 0, offLaw = 0, offClass = 0;
    float maxHw = 0.0f, maxD = 0.0f;
    for (int j = 0; j < f->cellsZ(); ++j)
        for (int i = 0; i < f->cellsX(); ++i) {
            const float wx = f->originX() + (i + 0.5f) * cs, wz = f->originZ() + (j + 0.5f) * cs;
            const int ord = f->orderAt(wx, wz);
            if (ord < 1) continue;
            ++river;
            const float aKm2 = static_cast<float>(f->accumAt(wx, wz)) * cs * cs * 1e-6f;
            const float hw = f->halfWidthAt(wx, wz), d = f->fullDepthAt(wx, wz);
            if (std::fabs(hw - FlowField::lawHalfWidth(aKm2)) > 1e-3f * FlowField::lawHalfWidth(aKm2) ||
                std::fabs(d - FlowField::lawDepth(aKm2)) > 1e-3f * FlowField::lawDepth(aKm2)) ++offLaw;
            const bool c = f->carvedRiverAt(wx, wz);
            if (c != (d >= FlowField::kRiverCarveDepth)) ++offClass;
            if (c) ++carved;
            maxHw = std::max(maxHw, hw);
            maxD = std::max(maxD, d);
        }
    std::printf("[ M2b-2 ] v2 law: %d river cells, %d carved (depth >= %.1f u), widest half %.2f u, deepest %.2f u; off-law %d, off-class %d\n",
                river, carved, FlowField::kRiverCarveDepth, maxHw, maxD, offLaw, offClass);
    ASSERT_GT(river, 0);
    EXPECT_EQ(offLaw, 0) << "river cells whose width/depth is not the law at their area";
    EXPECT_EQ(offClass, 0) << "river cells whose class disagrees with their centreline depth";
    EXPECT_GT(carved, 0) << "no carved river in the box: a trunk of >= 5.7 km2 must carve";
    EXPECT_NEAR(f->maxHalfWidth(), maxHw, 1e-4f);
}

// An order-3 stream shallower than the carve threshold is a CREEK on the generator's output: no
// voxel carve, the 1/3 u recess (creekBed), carvedRiver false. Red on the committed code: order
// >= 3 carves lround(depth) = 0 and sets no creekBed — a dry tagged bed.
TEST(WorldSheetGeneratorTest, CreekVsRiverIsADepthNotAnOrder) {
    WorldGenerator v2(WorldGenerator::GenerationType::Perlin, 20260816u);
    WorldRecipe r = v2Recipe(20260816u);
    r.creekCarve = true;
    v2.applyRecipe(r);
    const FlowField* f = v2.riverNetwork();
    ASSERT_TRUE(f);
    const float cs = f->cellSize();
    // Find a shallow order>=3 cell well inside the box, then the carved-line columns around it:
    // the generator carves along the meander-warped line, so scan a window for columns reporting
    // this class of channel (riverOrder >= 3) and check what the generator did with them.
    int checked = 0, wrong = 0;
    for (int j = 64; j < f->cellsZ() - 64 && checked < 200; j += 7)
        for (int i = 64; i < f->cellsX() - 64 && checked < 200; i += 7) {
            const float wx = f->originX() + (i + 0.5f) * cs, wz = f->originZ() + (j + 0.5f) * cs;
            if (f->orderAt(wx, wz) < 3 || f->fullDepthAt(wx, wz) >= FlowField::kRiverCarveDepth) continue;
            for (int dz = -40; dz <= 40 && checked < 200; dz += 2)
                for (int dx = -40; dx <= 40 && checked < 200; dx += 2) {
                    const auto c = v2.sampleSurface(static_cast<int>(wx) + dx, static_cast<int>(wz) + dz);
                    if (c.riverOrder < 3 || c.channelDepth < 0.15f) continue;
                    ++checked;
                    if (c.carvedRiver || !c.creekBed) ++wrong;
                }
        }
    std::printf("[ M2b-2 ] shallow order>=3 columns checked %d, misclassified %d\n", checked, wrong);
    ASSERT_GT(checked, 0) << "no shallow order>=3 channel columns found in the box";
    EXPECT_EQ(wrong, 0) << "a 0.15-0.5 u deep order>=3 column was carved or got no creek recess";
}
