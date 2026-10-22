#include <gtest/gtest.h>

#include "core/CivPlan.h"
#include "core/WorldForgePlan.h"
#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"
#include "core/WorldSheet.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Phyxel;

// ============================================================================
// CivPlanTest — M3a (docs/WorldForgeV2Plan.md M3, D18–D20): hundreds of sites over the whole
// continent, five tiers nested by Christaller's ladder, targets DERIVED from spacing and land
// area, deterministic, never in water or on a cliff, pins refused with a reason. Red on the
// committed code: the continent-wide plan does not exist (the V1 box siting clamps 3–8).
// ============================================================================
namespace {

WorldRecipe civRecipe(uint32_t seed) {
    WorldRecipe r;
    r.version = 2;
    r.type = "Perlin";
    r.seed = seed;
    r.worldforge.enabled = true;
    r.worldforge.civ = true;
    r.worldforge.siteCount = 8;
    r.worldforge.regionRadius = 2048;
    r.worldforge.minSpacing = 400;
    return r;
}

float cellDist(const CivPlan::Site& a, const CivPlan::Site& b, int n) {
    const int ai = a.cell % n, aj = a.cell / n, bi = b.cell % n, bj = b.cell / n;
    return std::sqrt(static_cast<float>((ai - bi) * (ai - bi) + (aj - bj) * (aj - bj)));
}

}  // namespace

TEST(CivPlanTest, HundredsOfSitesAtTheLadderTargets) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(civRecipe(20260816u));
    const CivPlan* civ = g.civPlan();
    ASSERT_TRUE(civ) << "no civ plan on a v2 world with worldforge.civ on";
    int total = 0, targetTotal = 0;
    std::printf("[ M3a ] land %.0f km2, bake %.1f ms:", civ->landKm2(), civ->bakeMs());
    for (int t = 0; t < CivPlan::kTiers; ++t) {
        std::printf(" %s %d/%d", CivPlan::tierName(t), civ->count(t), civ->target(t));
        total += civ->count(t); targetTotal += civ->target(t);
    }
    std::printf("; total %d/%d; refusals %zu\n", total, targetTotal, civ->refusals().size());
    for (const auto& r : civ->refusals()) std::printf("    refusal: %s\n", r.c_str());
    EXPECT_GE(total, 100) << "hundreds of sites: the continent should carry well over a hundred";
    EXPECT_GE(civ->landKm2(), 1000.0f);
    // targets are the ladder's, and the plan meets most of them (honest degradation is logged)
    EXPECT_GE(total, targetTotal / 2) << "fewer than half the derived targets were placed";
    for (int t = 0; t < CivPlan::kTiers; ++t)
        if (civ->target(t) >= 3) EXPECT_GE(civ->count(t), civ->target(t) / 2) << "tier " << CivPlan::tierName(t);
    EXPECT_LT(civ->bakeMs(), 10000.0f) << "plan bake under the 10 s gate (this build config)";
}

TEST(CivPlanTest, SpacingHoldsAgainstEverySiteOfTheSameOrHigherTier) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(civRecipe(20260816u));
    const CivPlan* civ = g.civPlan();
    ASSERT_TRUE(civ);
    const int n = g.macroSheet()->cells();
    int violations = 0; float worst = 1e9f;
    for (const auto& a : civ->sites())
        for (const auto& b : civ->sites()) {
            if (a.id == b.id || b.tier > a.tier) continue;      // b constrains a only if b's tier is a's or higher
            const float d = cellDist(a, b, n);
            const float need = civ->spacingCells(a.tier);
            if (d < need - 1e-3f) { ++violations; worst = std::min(worst, d / need); }
        }
    std::printf("[ M3a ] spacing: %zu sites, %d violations (worst %.2f of the ladder)\n", civ->sites().size(), violations, violations ? worst : 1.0f);
    EXPECT_EQ(violations, 0);
}

TEST(CivPlanTest, HierarchyRatioIsCentralPlace) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(civRecipe(20260816u));
    const CivPlan* civ = g.civPlan();
    ASSERT_TRUE(civ);
    // adjacent tiers whose targets both reach 5: the count ratio tracks the spacing ratio squared
    // (K = 3 on the √3 ladder) within [2, 4.5]
    int checked = 0;
    for (int t = 0; t + 1 < CivPlan::kTiers; ++t) {
        if (civ->count(t) < 5 || civ->count(t + 1) < 5) continue;
        const float ratio = static_cast<float>(civ->count(t + 1)) / civ->count(t);
        std::printf("[ M3a ] %s:%s = %d:%d (ratio %.2f)\n", CivPlan::tierName(t + 1), CivPlan::tierName(t), civ->count(t + 1), civ->count(t), ratio);
        EXPECT_GE(ratio, 2.0f); EXPECT_LE(ratio, 4.5f);
        ++checked;
    }
    EXPECT_GT(checked, 0) << "no adjacent tier pair with 5+ sites each";
}

TEST(CivPlanTest, NoSiteInWaterOrOnACliff) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(civRecipe(20260816u));
    const CivPlan* civ = g.civPlan();
    ASSERT_TRUE(civ);
    const MacroSheet* M = g.macroSheet().get();
    const int n = M->cells();
    int bad = 0;
    for (const auto& s : civ->sites()) {
        const MacroCell& c = M->cell(s.cell % n, s.cell / n);
        if (c.baseHeight <= g.getTerrainParams().seaLevelY) ++bad;
        if (c.filled > c.baseHeight + 1e-3f) ++bad;
        if (s.slope > g.getWorldForgeParams().clamped().civSlopeCap[s.tier] + 1e-6f) ++bad;
    }
    EXPECT_EQ(bad, 0);
}

TEST(CivPlanTest, DeterministicAcrossGenerators) {
    WorldGenerator a(WorldGenerator::GenerationType::Perlin, 20260816u), b(WorldGenerator::GenerationType::Perlin, 20260816u);
    a.applyRecipe(civRecipe(20260816u)); b.applyRecipe(civRecipe(20260816u));
    ASSERT_TRUE(a.civPlan() && b.civPlan());
    ASSERT_EQ(a.civPlan()->sites().size(), b.civPlan()->sites().size());
    for (size_t i = 0; i < a.civPlan()->sites().size(); ++i) {
        EXPECT_EQ(a.civPlan()->sites()[i].cell, b.civPlan()->sites()[i].cell);
        EXPECT_EQ(a.civPlan()->sites()[i].tier, b.civPlan()->sites()[i].tier);
    }
}

TEST(CivPlanTest, PinsInWaterAreRefusedWithTheReason) {
    // The civ plan sees the MACRO sea (the 512 u sheet's base below sea level), not the meso bake's
    // (the baseline world's inland sea at (-2576, -592) is a meso-scale pit the macro base does not
    // show - measured: that pin is macro LAND). So the pin goes on a macro sub-sea cell found from
    // the sheet itself, nearest the origin.
    WorldGenerator probe(WorldGenerator::GenerationType::Perlin, 20260816u);
    probe.applyRecipe(civRecipe(20260816u));
    const MacroSheet* M = probe.macroSheet().get();
    ASSERT_TRUE(M);
    const int n = M->cells();
    const float cu = M->cellU();
    glm::ivec2 seaPin{0, 0}; float best = 1e30f; bool found = false;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            if (M->cell(i, j).baseHeight <= probe.getTerrainParams().seaLevelY) {
                const float x = M->originU() + (i + 0.5f) * cu, z = M->originU() + (j + 0.5f) * cu;
                if (x * x + z * z < best) { best = x * x + z * z; seaPin = {static_cast<int>(x), static_cast<int>(z)}; found = true; }
            }
    ASSERT_TRUE(found) << "no macro sub-sea cell on the continent";
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    WorldRecipe r = civRecipe(20260816u);
    r.worldforge.sitePins.push_back(seaPin);
    g.applyRecipe(r);
    const CivPlan* civ = g.civPlan();
    ASSERT_TRUE(civ);
    const std::string key = "pin (" + std::to_string(seaPin.x) + "," + std::to_string(seaPin.y) + ")";
    bool refused = false;
    for (const auto& s : civ->refusals()) if (s.find(key) != std::string::npos) refused = true;
    std::printf("[ M3a ] sea pin at (%d,%d): refused %d; refusals:", seaPin.x, seaPin.y, refused ? 1 : 0);
    for (const auto& s : civ->refusals()) std::printf(" | %s", s.c_str());
    std::printf("\n");
    EXPECT_TRUE(refused) << "a pin in the sea was not refused with its reason";
    for (const auto& s : civ->sites()) EXPECT_FALSE(s.pinned && std::fabs(s.pos.x - seaPin.x) < 600.0f && std::fabs(s.pos.y - seaPin.y) < 600.0f);
}

// The realized box plan (the V1 WorldForgePlan that SettlementBuildService builds) is seeded by
// the civ sites that fall inside the hydrology box, tiers mapped city/town → town, village →
// village, hamlet/outpost → hamlet; the 3–8 clamp no longer applies.
TEST(CivPlanTest, TheBoxPlanIsSeededByTheCivSites) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(civRecipe(20260816u));
    const CivPlan* civ = g.civPlan();
    ASSERT_TRUE(civ);
    const WorldForgePlan* box = g.worldForge();
    ASSERT_TRUE(box);
    const float half = 16384.0f;
    int civInBox = 0;
    for (const auto& s : civ->sites())
        if (s.tier <= CivPlan::Hamlet && std::fabs(s.pos.x) < half && std::fabs(s.pos.y) < half) ++civInBox;
    int matched = 0;
    for (const auto& bs : box->sites()) {
        bool found = false;
        for (const auto& s : civ->sites())
            if (std::fabs(s.pos.x - bs.pos.x) <= 512.0f && std::fabs(s.pos.y - bs.pos.y) <= 512.0f) { found = true; break; }
        if (found) ++matched;
    }
    std::printf("[ M3a ] civ sites in the box (city..hamlet): %d; box plan sites %zu, %d within a macro cell of a civ site\n",
                civInBox, box->sites().size(), matched);
    // the box plan is EXACTLY the civ sites inside the box (the baseline box holds a tenth of the
    // continent's land: 6 of the 157 sites), and the V1 3-8 clamp no longer applies when civ is on
    EXPECT_EQ(static_cast<int>(box->sites().size()), civInBox) << "the box plan is not the civ sites inside the box";
    EXPECT_EQ(matched, static_cast<int>(box->sites().size())) << "a box-plan site is not a civ site";
    WorldForgeParams many = civRecipe(1).worldforge;
    many.siteCount = 64;
    EXPECT_EQ(many.clamped().siteCount, 64) << "the 3-8 clamp still applies with civ on";
    many.civ = false;
    EXPECT_EQ(many.clamped().siteCount, 8) << "the V1 clamp must stay for V1 worlds";
}
