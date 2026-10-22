#include <gtest/gtest.h>

#include "core/CivPlan.h"
#include "core/WorldForgeParams.h"
#include "core/WorldSheet.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <vector>

using namespace Phyxel;

// ============================================================================
// CivPlanStressTest — the M3a stress phase (CLAUDE.md "Stress Test Phase"): the civ plan on
// synthetic macro sheets pushed to the degenerate and the extreme. Invariants asserted at EVERY
// site, not in aggregate: no site in water/basin, none above its tier's slope cap, no two sites
// in one cell, spacing against every site of the same or a higher tier (pinned included), every
// shortfall and every refused pin named with its reason, targets derived (never typed).
// ============================================================================
namespace {

constexpr float kSea = 16.0f;
constexpr float kCellU = 512.0f;

std::shared_ptr<MacroSheet> makeSheet(int n, const std::function<void(int, int, MacroCell&)>& fill) {
    auto M = std::make_shared<MacroSheet>(n, kCellU, 0x5EEDull);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) fill(i, j, M->cell(i, j));
    return M;
}

// A temperate, moist climate everywhere (raw planes in [-1,1]: t01 0.55 inside the food bell,
// m01 0.6 inside the moisture bell) so suitability is driven by the sheet alone.
void temperate(float, float, float& t, float& m) { t = 0.1f; m = 0.2f; }

WorldForgeParams civParams() {
    WorldForgeParams p;
    p.enabled = true;
    p.civ = true;
    return p;
}

CivPlan::Inputs inputs(const MacroSheet& M) {
    CivPlan::Inputs in;
    in.macro = &M;
    in.seaLevel = kSea;
    in.seed = 7u;
    in.climate = temperate;
    return in;
}

// A fertile plain: flat land at Y 50 with a sea strait down column n/2 (water near, not in).
void fertilePlain(int i, int /*j*/, MacroCell& c, int n) {
    c.baseHeight = (i == n / 2) ? 0.0f : 50.0f;
    c.filled = c.baseHeight;
    c.continentalness = 0.6f;
}

struct Check {
    int sharedCell = 0, inWater = 0, overCap = 0, spacing = 0;
    float worstSpacing = 1.0f;
};

Check checkInvariants(const CivPlan& civ, const MacroSheet& M, const WorldForgeParams& P) {
    Check r;
    const int n = M.cells();
    std::set<int> cells;
    const WorldForgeParams C = P.clamped();
    for (const auto& s : civ.sites()) {
        if (!cells.insert(s.cell).second) ++r.sharedCell;
        const MacroCell& c = M.cell(s.cell % n, s.cell / n);
        if (c.baseHeight <= kSea || c.filled > c.baseHeight + 1e-3f) ++r.inWater;
        if (!s.pinned && s.slope > C.civSlopeCap[s.tier] + 1e-6f) ++r.overCap;
        if (s.pinned && s.slope > C.civSlopeCap[CivPlan::Town] + 1e-6f) ++r.overCap;
    }
    for (const auto& a : civ.sites())
        for (const auto& b : civ.sites()) {
            if (a.id == b.id || b.tier > a.tier) continue;
            const int ai = a.cell % n, aj = a.cell / n, bi = b.cell % n, bj = b.cell / n;
            const float d = std::sqrt(static_cast<float>((ai - bi) * (ai - bi) + (aj - bj) * (aj - bj)));
            const float need = civ.spacingCells(a.tier);
            if (d < need - 1e-3f) { ++r.spacing; r.worstSpacing = std::min(r.worstSpacing, d / need); }
        }
    return r;
}

void printPlan(const char* tag, const CivPlan& civ) {
    std::printf("[ M3a stress ] %s: land %.0f km2, %zu sites, bake %.1f ms;", tag, civ.landKm2(), civ.sites().size(), civ.bakeMs());
    for (int t = 0; t < CivPlan::kTiers; ++t) std::printf(" %s %d/%d", CivPlan::tierName(t), civ.count(t), civ.target(t));
    std::printf("; refusals %zu\n", civ.refusals().size());
    for (const auto& s : civ.refusals()) std::printf("    %s\n", s.c_str());
}

}  // namespace

// Degenerate: a continent that is all sea. No land, no targets, no sites, no crash; every pin
// refused as water.
TEST(CivPlanStressTest, AllSeaContinentHasNoSitesAndRefusesEveryPin) {
    auto M = makeSheet(64, [](int, int, MacroCell& c) { c.baseHeight = 0.0f; c.filled = 0.0f; });
    WorldForgeParams P = civParams();
    for (int k = 0; k < 20; ++k) P.sitePins.push_back({-10000 + k * 1000, 3000});
    auto civ = CivPlan::bake(inputs(*M), P);
    printPlan("all sea", *civ);
    EXPECT_EQ(civ->landKm2(), 0.0f);
    EXPECT_TRUE(civ->sites().empty());
    for (int t = 0; t < CivPlan::kTiers; ++t) EXPECT_EQ(civ->target(t), 0) << CivPlan::tierName(t);
    int waterRefusals = 0;
    for (const auto& s : civ->refusals()) if (s.find("in water") != std::string::npos) ++waterRefusals;
    EXPECT_EQ(waterRefusals, 20) << "every pin on the sea must be refused as water";
    EXPECT_NO_THROW(civ->toJson().dump());
}

// Degenerate: a continent that is all steep ridge (slope 0.4 everywhere, above every cap). All
// land, real targets, ZERO sites, and one honest shortfall refusal per tier.
TEST(CivPlanStressTest, AllMountainContinentDegradesHonestlyToZeroSites) {
    const int n = 100;
    auto M = makeSheet(n, [](int i, int, MacroCell& c) { c.baseHeight = 100.0f + i * kCellU * 0.4f; c.filled = c.baseHeight; });
    auto civ = CivPlan::bake(inputs(*M), civParams());
    printPlan("all mountain", *civ);
    EXPECT_GT(civ->landKm2(), 2000.0f);
    int targets = 0;
    for (int t = 0; t < CivPlan::kTiers; ++t) targets += civ->target(t);
    EXPECT_GT(targets, 50) << "a 2600 km2 continent derives real targets";
    EXPECT_TRUE(civ->sites().empty()) << "no cell is under any tier's slope cap";
    int shortfalls = 0, tiersWithTarget = 0;
    for (const auto& s : civ->refusals()) if (s.find("unplaced") != std::string::npos) ++shortfalls;
    for (int t = 0; t < CivPlan::kTiers; ++t) if (civ->target(t) > 0) ++tiersWithTarget;
    EXPECT_EQ(shortfalls, tiersWithTarget) << "one named shortfall per tier that had a target";
}

// Extreme density: the fertile plain at civDensityScale 10 — targets scale by ten, the plan
// fills most of them, and spacing still holds for EVERY pair at every tier.
TEST(CivPlanStressTest, DensityTenFillsTheLadderWithSpacingIntact) {
    const int n = 200;
    auto M = makeSheet(n, [n](int i, int j, MacroCell& c) { fertilePlain(i, j, c, n); });
    WorldForgeParams P1 = civParams();
    auto base = CivPlan::bake(inputs(*M), P1);
    WorldForgeParams P10 = civParams();
    P10.civDensityScale = 10.0f;
    auto dense = CivPlan::bake(inputs(*M), P10);
    printPlan("plain x1", *base);
    printPlan("plain x10", *dense);
    const Check c1 = checkInvariants(*base, *M, P1), c10 = checkInvariants(*dense, *M, P10);
    EXPECT_EQ(c1.spacing, 0); EXPECT_EQ(c1.sharedCell, 0); EXPECT_EQ(c1.inWater, 0); EXPECT_EQ(c1.overCap, 0);
    EXPECT_EQ(c10.spacing, 0) << "worst " << c10.worstSpacing;
    EXPECT_EQ(c10.sharedCell, 0); EXPECT_EQ(c10.inWater, 0); EXPECT_EQ(c10.overCap, 0);
    for (int t = 0; t < CivPlan::kTiers; ++t) {
        EXPECT_GE(dense->target(t), base->target(t) * 9) << CivPlan::tierName(t) << " target did not scale";
        EXPECT_LE(dense->count(t), dense->target(t)) << "count above target";
        EXPECT_NEAR(dense->spacingCells(t) * std::sqrt(10.0f), base->spacingCells(t), 1e-2f) << "x10 density = the spacing over sqrt(10)";
    }
    EXPECT_GT(dense->sites().size(), base->sites().size() * 5u) << "x10 density should place many more sites";
    // the water strait made every cell viable and the plain is flat: each tier reaches at least
    // half its ladder target (greedy packing on a square sheet cannot always seat the last one)
    for (int t = 0; t < CivPlan::kTiers; ++t) EXPECT_GE(base->count(t), base->target(t) / 2) << CivPlan::tierName(t);
}

// Extreme extent: a 400x400 sheet (4x the shipped continent's cells, ~41 000 km2 of land) — the
// plan stays correct at every site and its cost is measured.
TEST(CivPlanStressTest, FourTimesTheContinentKeepsEveryInvariant) {
    const int n = 400;
    auto M = makeSheet(n, [n](int i, int j, MacroCell& c) {
        fertilePlain(i, j, c, n);
        // gentle rolling relief so the score has structure: 0..40 u over 16 cells
        if (c.baseHeight > kSea) c.baseHeight += 20.0f * (1.0f + std::sin(i * 0.39f) * std::cos(j * 0.31f));
        c.filled = c.baseHeight;
    });
    WorldForgeParams P = civParams();
    auto civ = CivPlan::bake(inputs(*M), P);
    printPlan("400x400", *civ);
    const Check c = checkInvariants(*civ, *M, P);
    EXPECT_EQ(c.spacing, 0) << "worst " << c.worstSpacing;
    EXPECT_EQ(c.sharedCell, 0); EXPECT_EQ(c.inWater, 0); EXPECT_EQ(c.overCap, 0);
    EXPECT_GT(civ->sites().size(), 500u);
    for (int t = 0; t < CivPlan::kTiers; ++t) EXPECT_LE(civ->count(t), civ->target(t));
    EXPECT_LT(civ->bakeMs(), 60000.0f) << "bake cost gate (this build config)";
}

// Degenerate: a 4x4 sheet — 4 km2 of land derives no target at all; no crash, no site.
TEST(CivPlanStressTest, TinySheetDerivesNothingAndDoesNotCrash) {
    auto M = makeSheet(4, [](int i, int j, MacroCell& c) { fertilePlain(i, j, c, 4); });
    WorldForgeParams P = civParams();
    P.civDensityScale = 10.0f;
    auto civ = CivPlan::bake(inputs(*M), P);
    printPlan("4x4", *civ);
    EXPECT_TRUE(civ->sites().empty());
    EXPECT_TRUE(civ->refusals().empty()) << "a zero target is not a shortfall";
    EXPECT_NO_THROW(civ->toJson().dump());
}

// Pins at scale: 300 pins of five kinds on the fertile plain. Land pins seat as towns; sea,
// outside-the-continent and steep pins are refused with their reason; a pin on a cell that is
// already a site, or within the town spacing of an earlier pin, is refused too — never seated
// on top of another site, never a silent spacing break. Every surviving site keeps every
// invariant (pinned towns included).
TEST(CivPlanStressTest, ThreeHundredPinsEveryKindRefusedWithItsReason) {
    const int n = 200;
    auto M = makeSheet(n, [n](int i, int j, MacroCell& c) {
        fertilePlain(i, j, c, n);
        if (j >= 190) { c.baseHeight = 100.0f + i * kCellU * 0.4f; c.filled = c.baseHeight; }   // a steep strip
    });
    WorldForgeParams P = civParams();
    const float o = M->originU();
    auto at = [&](int i, int j) { return glm::ivec2(static_cast<int>(o + (i + 0.5f) * kCellU), static_cast<int>(o + (j + 0.5f) * kCellU)); };
    // 100 land pins on a lattice far enough apart for the town spacing (36 km = 70.3 cells):
    // a 10x10 lattice needs >= 71 cells of pitch, which a 200-cell sheet cannot hold — so use
    // 2x2 = 4 well-spaced towns and 96 more on a tight lattice that MUST be refused for spacing.
    int landPins = 0, tightPins = 0;
    for (int a = 0; a < 2; ++a) for (int b = 0; b < 2; ++b) { P.sitePins.push_back(at(20 + a * 100, 20 + b * 100)); ++landPins; }
    for (int k = 0; k < 96; ++k) { P.sitePins.push_back(at(30 + (k % 12) * 3, 30 + (k / 12) * 3)); ++tightPins; }
    for (int k = 0; k < 50; ++k) P.sitePins.push_back(at(20, 20));                   // 50 duplicates of the first pin
    for (int k = 0; k < 50; ++k) P.sitePins.push_back(at(n / 2, 5 + k));            // 50 in the strait
    for (int k = 0; k < 50; ++k) P.sitePins.push_back({static_cast<int>(o) - 5000 - k * 100, 0});   // 50 outside
    for (int k = 0; k < 50; ++k) P.sitePins.push_back(at(10 + k * 3, 195));         // 50 on the steep strip
    ASSERT_EQ(P.sitePins.size(), 300u);
    auto civ = CivPlan::bake(inputs(*M), P);
    printPlan("300 pins", *civ);
    int seatedPins = 0;
    for (const auto& s : civ->sites()) if (s.pinned) ++seatedPins;
    int rWater = 0, rOutside = 0, rSteep = 0, rDuplicate = 0, rSpacing = 0;
    for (const auto& r : civ->refusals()) {
        if (r.find("in water") != std::string::npos) ++rWater;
        else if (r.find("outside") != std::string::npos) ++rOutside;
        else if (r.find("steeper") != std::string::npos) ++rSteep;
        else if (r.find("already a site") != std::string::npos) ++rDuplicate;
        else if (r.find("within the town spacing") != std::string::npos) ++rSpacing;
    }
    std::printf("[ M3a stress ] pins: seated %d; refused water %d outside %d steep %d duplicate %d spacing %d\n",
                seatedPins, rWater, rOutside, rSteep, rDuplicate, rSpacing);
    EXPECT_EQ(rWater, 50);
    EXPECT_EQ(rOutside, 50);
    EXPECT_EQ(rSteep, 50);
    EXPECT_EQ(rDuplicate, 50) << "a pin on a cell that is already a site must be refused, not stacked";
    EXPECT_EQ(rSpacing, tightPins) << "every tight-lattice pin is within the town spacing of an earlier pin";
    EXPECT_EQ(seatedPins, landPins);
    const Check c = checkInvariants(*civ, *M, P);
    EXPECT_EQ(c.sharedCell, 0) << "two sites in one cell";
    EXPECT_EQ(c.spacing, 0) << "worst " << c.worstSpacing;
    EXPECT_EQ(c.inWater, 0); EXPECT_EQ(c.overCap, 0);
    int sum = 0;
    for (int t = 0; t < CivPlan::kTiers; ++t) sum += civ->count(t);
    EXPECT_EQ(static_cast<int>(civ->sites().size()), sum) << "the per-tier counts must add up to the sites";
}
