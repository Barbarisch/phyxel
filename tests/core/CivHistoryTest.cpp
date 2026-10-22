#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "core/CivPlan.h"
#include "core/WorldForgeParams.h"
#include "core/WorldForgePlan.h"
#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"
#include "core/WorldSheet.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace Phyxel;

// ============================================================================
// CivHistoryTest — M3b (docs/WorldForgeV2Plan.md M3b design check, D21–D24): the history ledger
// over the M3a plan. 12 epochs of 25 years, one −45 % shock at epoch 9 (the England 1086 → 1377
// arc), sites found/grow/decline/abandon, provinces seated by cities and towns. Red on the M3a
// code: the fields exist, nothing simulates (every site stays live, no provinces, no events).
// ============================================================================
namespace {

WorldRecipe civRecipe(uint32_t seed, int epochs) {
    WorldRecipe r;
    r.version = 2;
    r.type = "Perlin";
    r.seed = seed;
    r.worldforge.enabled = true;
    r.worldforge.civ = true;
    r.worldforge.civEpochs = epochs;        // D23 default arc: shock epoch 9, −45 %
    r.worldforge.siteCount = 8;
    r.worldforge.regionRadius = 2048;
    r.worldforge.minSpacing = 400;
    return r;
}

std::set<std::pair<int, int>> siteSet(const CivPlan& c) {
    std::set<std::pair<int, int>> s;
    for (const auto& x : c.sites()) s.insert({x.cell, x.tier});
    return s;
}

void printLedger(const char* tag, const CivPlan& civ) {
    int live = 0, declining = 0, abandoned = 0, events = 0;
    for (const auto& s : civ.sites()) {
        if (s.state == CivPlan::Live) ++live; else if (s.state == CivPlan::Declining) ++declining; else ++abandoned;
        events += static_cast<int>(s.history.size());
    }
    std::printf("[ M3b ] %s: epochs %d, %zu sites: live %d, declining %d, abandoned %d; %d events; %zu provinces\n",
                tag, civ.epochs(), civ.sites().size(), live, declining, abandoned, events, civ.provinces().size());
}

}  // namespace

// D23: `civEpochs 0` is M3a's plan bit for bit — the same cells and tiers, every site live with
// one founding event and no ruins; and the 12-epoch plan keeps the SAME site set (history never
// adds or moves a site, D21 never changes a tier).
TEST(CivHistoryTest, EpochsZeroIsM3aBitForBit) {
    WorldGenerator a(WorldGenerator::GenerationType::Perlin, 20260816u), b(WorldGenerator::GenerationType::Perlin, 20260816u);
    a.applyRecipe(civRecipe(20260816u, 0));
    b.applyRecipe(civRecipe(20260816u, 12));
    ASSERT_TRUE(a.civPlan() && b.civPlan());
    printLedger("epochs 0", *a.civPlan());
    printLedger("epochs 12", *b.civPlan());
    EXPECT_EQ(a.civPlan()->epochs(), 0);
    EXPECT_EQ(b.civPlan()->epochs(), 12);
    EXPECT_EQ(siteSet(*a.civPlan()), siteSet(*b.civPlan())) << "history changed the site set or a tier (D21)";
    for (const auto& s : a.civPlan()->sites()) {
        EXPECT_EQ(s.state, CivPlan::Live);
        EXPECT_EQ(s.founded, 0);
    }
}

// The control number (GROUNDED, §10 F4g): 2 811 deserted medieval villages of ≈ 13 000 Domesday
// places ≈ 20 % of ever-founded village-and-below sites abandoned by the end of the arc. The
// prediction written before the first run — "post-shock viability against the plan's own
// kMinViable lands the baseline in 15–25 %" — FAILED (0 of 148 at −45 %; 148 of 154 at −90 %), so
// the rate is now an INPUT (civAbandonShare 0.20) and this test checks the ledger applies it to
// the MARGINAL sites: the abandoned ones are the lowest-scoring of their tier, never a seat.
TEST(CivHistoryTest, AbandonedFractionMatchesTheDesertedVillageRate) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(civRecipe(20260816u, 12));
    const CivPlan* civ = g.civPlan();
    ASSERT_TRUE(civ);
    printLedger("baseline 12 epochs", *civ);
    int low = 0, lowAbandoned = 0, high = 0, highAbandoned = 0;
    for (const auto& s : civ->sites()) {
        if (s.tier >= CivPlan::Village) { ++low; if (s.state == CivPlan::Abandoned) ++lowAbandoned; }
        else { ++high; if (s.state == CivPlan::Abandoned) ++highAbandoned; }
    }
    const float frac = low ? static_cast<float>(lowAbandoned) / low : 0.0f;
    std::printf("[ M3b ] abandoned: village-and-below %d of %d (%.1f %%), city/town %d of %d\n", lowAbandoned, low, frac * 100.0f, highAbandoned, high);
    EXPECT_GE(frac, 0.15f) << "fewer ruins than the deserted-village rate";
    EXPECT_LE(frac, 0.25f) << "more ruins than the deserted-village rate";
    EXPECT_EQ(highAbandoned, 0) << "a city or town was abandoned (the shock does not empty the seats of the hierarchy)";
    // marginal, not random: within each tier every ruin scores at or below every live site
    int notMarginal = 0;
    for (const auto& r : civ->sites()) {
        if (r.state != CivPlan::Abandoned) continue;
        for (const auto& l : civ->sites())
            if (l.tier == r.tier && l.state == CivPlan::Live && l.founded <= g.getWorldForgeParams().clamped().civShockEpoch && l.score < r.score - 1e-6f) ++notMarginal;
    }
    EXPECT_EQ(notMarginal, 0) << "a ruin outscored a live site of its tier";
}

// Every ruin's ledger reads founded → (grew…) → shock → declined → abandoned, in epoch order, and
// every live site was founded at or before the last epoch with a population above zero.
TEST(CivHistoryTest, EveryRuinHasAFoundedEvent) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(civRecipe(20260816u, 12));
    const CivPlan* civ = g.civPlan();
    ASSERT_TRUE(civ);
    int ruins = 0, bad = 0;
    for (const auto& s : civ->sites()) {
        ASSERT_FALSE(s.history.empty()) << "site " << s.id << " has no ledger";
        EXPECT_EQ(s.history.front().kind, "founded") << "site " << s.id;
        EXPECT_EQ(s.history.front().epoch, s.founded) << "site " << s.id;
        for (size_t i = 1; i < s.history.size(); ++i) EXPECT_LE(s.history[i - 1].epoch, s.history[i].epoch) << "site " << s.id << " ledger out of order";
        EXPECT_LE(s.founded, civ->epochs());
        EXPECT_GE(s.peak, s.founded);
        if (s.state == CivPlan::Abandoned) {
            ++ruins;
            const bool shock = std::any_of(s.history.begin(), s.history.end(), [](const CivPlan::Event& e) { return e.kind == "shock"; });
            const bool declined = std::any_of(s.history.begin(), s.history.end(), [](const CivPlan::Event& e) { return e.kind == "declined"; });
            if (!shock || !declined || s.history.back().kind != "abandoned" || s.population > 0.0f) ++bad;
        } else {
            EXPECT_GT(s.population, 0.0f) << "live site " << s.id << " has no population";
        }
    }
    std::printf("[ M3b ] ledgers: %zu sites, %d ruins, %d malformed\n", civ->sites().size(), ruins, bad);
    EXPECT_GT(ruins, 0) << "no ruin at all after a −45 % shock";
    EXPECT_EQ(bad, 0);
}

// Provinces: every macro LAND cell belongs to exactly one province, every site to exactly one,
// every seat is a city or a town, and a seat is a member of its own province.
TEST(CivHistoryTest, ProvincesPartitionTheLand) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(civRecipe(20260816u, 12));
    const CivPlan* civ = g.civPlan();
    ASSERT_TRUE(civ);
    const MacroSheet* M = g.macroSheet().get();
    const int n = M->cells();
    const float sea = g.getTerrainParams().seaLevelY;
    ASSERT_FALSE(civ->provinces().empty()) << "no provinces";
    int seats = 0, cellsAssigned = 0;
    std::map<int, int> memberOf;
    for (const auto& p : civ->provinces()) {
        ASSERT_GE(p.seat, 0); ASSERT_LT(p.seat, static_cast<int>(civ->sites().size()));
        EXPECT_LE(civ->sites()[p.seat].tier, CivPlan::Town) << "seat " << p.seat << " is not a city or town";
        EXPECT_TRUE(std::find(p.members.begin(), p.members.end(), p.seat) != p.members.end()) << "seat " << p.seat << " not in its own province";
        for (int m : p.members) ++memberOf[m];
        cellsAssigned += p.cells;
        ++seats;
    }
    int landCells = 0;
    for (int j = 0; j < n; ++j) for (int i = 0; i < n; ++i) {
        const MacroCell& c = M->cell(i, j);
        if (c.baseHeight > sea + 2.0f && !(c.filled > c.baseHeight + 1e-3f)) ++landCells;
    }
    std::printf("[ M3b ] provinces: %d seats, %d of %d land cells assigned, %zu of %zu sites members\n", seats, cellsAssigned, landCells, memberOf.size(), civ->sites().size());
    EXPECT_EQ(cellsAssigned, landCells) << "provinces do not partition the land";
    EXPECT_EQ(memberOf.size(), civ->sites().size()) << "a site belongs to no province";
    for (const auto& kv : memberOf) EXPECT_EQ(kv.second, 1) << "site " << kv.first << " in " << kv.second << " provinces";
    int cityTown = 0;
    for (const auto& s : civ->sites()) if (s.tier <= CivPlan::Town) ++cityTown;
    EXPECT_EQ(seats, cityTown) << "every city and town seats a province";
}

// Live sites keep every M3a invariant after the history ran (spacing against same-or-higher live
// sites; no site in water or above its cap — the set is unchanged, so this pins that the ledger
// never relocates anything).
TEST(CivHistoryTest, LiveSitesKeepEveryM3aInvariant) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(civRecipe(20260816u, 12));
    const CivPlan* civ = g.civPlan();
    ASSERT_TRUE(civ);
    const int n = g.macroSheet()->cells();
    const WorldForgeParams P = g.getWorldForgeParams().clamped();
    int violations = 0, overCap = 0;
    for (const auto& a : civ->sites()) {
        if (a.state == CivPlan::Abandoned) continue;
        if (!a.pinned && a.slope > P.civSlopeCap[a.tier] + 1e-6f) ++overCap;
        for (const auto& b : civ->sites()) {
            if (a.id == b.id || b.tier > a.tier || b.state == CivPlan::Abandoned) continue;
            const int ai = a.cell % n, aj = a.cell / n, bi = b.cell % n, bj = b.cell / n;
            const float d = std::sqrt(static_cast<float>((ai - bi) * (ai - bi) + (aj - bj) * (aj - bj)));
            if (d < civ->spacingCells(a.tier) - 1e-3f) ++violations;
        }
    }
    EXPECT_EQ(violations, 0);
    EXPECT_EQ(overCap, 0);
}

// Stress along the ledger's axes: 64 epochs (the clamp), the shock at epoch 0, a 90 % shock, no
// shock at all. Invariants at every step: the site set never changes, no site is founded twice,
// ruins ≤ sites, ledgers stay ordered, the plan is deterministic across generators.
TEST(CivHistoryTest, StressEpochsShockAndDeterminism) {
    struct Rig { const char* tag; int epochs; int shock; float frac; };
    const Rig rigs[] = {{"64 epochs", 64, 9, 0.45f}, {"shock at 0", 12, 0, 0.45f}, {"90 % shock", 12, 9, 0.9f}, {"no shock", 12, -1, 0.45f}};
    std::set<std::pair<int, int>> base;
    {
        WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
        g.applyRecipe(civRecipe(20260816u, 0));
        ASSERT_TRUE(g.civPlan());
        base = siteSet(*g.civPlan());
    }
    for (const Rig& r : rigs) {
        WorldRecipe rec = civRecipe(20260816u, r.epochs);
        rec.worldforge.civShockEpoch = r.shock;
        rec.worldforge.civShockFraction = r.frac;
        WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u), h(WorldGenerator::GenerationType::Perlin, 20260816u);
        g.applyRecipe(rec); h.applyRecipe(rec);
        ASSERT_TRUE(g.civPlan() && h.civPlan());
        const CivPlan& civ = *g.civPlan();
        printLedger(r.tag, civ);
        EXPECT_EQ(siteSet(civ), base) << r.tag;
        int ruins = 0;
        for (size_t i = 0; i < civ.sites().size(); ++i) {
            const auto& s = civ.sites()[i];
            int founded = 0;
            for (size_t k = 0; k < s.history.size(); ++k) {
                if (s.history[k].kind == "founded") ++founded;
                if (k) EXPECT_LE(s.history[k - 1].epoch, s.history[k].epoch) << r.tag;
            }
            EXPECT_EQ(founded, 1) << r.tag << " site " << s.id << " founded " << founded << " times";
            if (s.state == CivPlan::Abandoned) ++ruins;
            const auto& t = h.civPlan()->sites()[i];
            EXPECT_EQ(s.state, t.state) << r.tag; EXPECT_EQ(s.founded, t.founded) << r.tag;
            EXPECT_FLOAT_EQ(s.population, t.population) << r.tag;
        }
        EXPECT_LE(ruins, static_cast<int>(civ.sites().size()));
        if (r.shock < 0) EXPECT_EQ(ruins, 0) << "ruins without a shock";
    }
}

// D22: a ruin is a state the structure forge cannot dress yet, so an abandoned civ site must not
// seed the box plan as a live settlement (red 2026-10-22 from the CivLab L4 raw JSON: the
// abandoned hamlet at (1280,-8960) came back as a live box hamlet at (1336,-8920)).
TEST(CivHistoryTest, RuinsAreNotSeededIntoTheBoxPlan) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(civRecipe(20260816u, 12));
    const CivPlan* civ = g.civPlan();
    const WorldForgePlan* box = g.worldForge();
    ASSERT_TRUE(civ && box);
    int ruinsInBox = 0, seededRuins = 0;
    for (const auto& s : civ->sites())
        if (s.state == CivPlan::Abandoned && s.tier <= CivPlan::Hamlet && std::fabs(s.pos.x) < 16384.0f - 512.0f && std::fabs(s.pos.y) < 16384.0f - 512.0f) ++ruinsInBox;
    for (const auto& bs : box->sites()) {
        const CivPlan::Site* nearest = nullptr; float best = 1e9f;
        for (const auto& s : civ->sites()) {
            const float d = std::hypot(s.pos.x - bs.pos.x, s.pos.y - bs.pos.y);
            if (d < best) { best = d; nearest = &s; }
        }
        if (nearest && best <= 100.0f && nearest->state == CivPlan::Abandoned) ++seededRuins;
    }
    std::printf("[ M3b ] box plan: %zu sites; abandoned civ sites inside the box %d, seeded as live %d\n", box->sites().size(), ruinsInBox, seededRuins);
    EXPECT_GT(ruinsInBox, 0) << "the baseline's 12-epoch arc should leave at least one ruin inside the box";
    EXPECT_EQ(seededRuins, 0) << "an abandoned civ site was seeded into the box plan";
}

// Audit 2026-10-22: the ledger's arithmetic was unasserted (mutating the growth or the shock multiply
// passed every test). Every "grew" value = the previous value × (1 + 0.114·(0.5 + score)); every
// "shock" value = the previous value × (1 − civShockFraction); a 90 % shock leaves every shocked
// site lower than a 45 % one; no site grows in its founding epoch.
TEST(CivHistoryTest, LedgerMathMatchesTheGrounding) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(civRecipe(20260816u, 12));
    WorldRecipe hard = civRecipe(20260816u, 12);
    hard.worldforge.civShockFraction = 0.9f;
    WorldGenerator h(WorldGenerator::GenerationType::Perlin, 20260816u);
    h.applyRecipe(hard);
    ASSERT_TRUE(g.civPlan() && h.civPlan());
    int grew = 0, shocks = 0, badGrow = 0, badShock = 0, grewAtFounding = 0, harderNotLower = 0;
    for (size_t i = 0; i < g.civPlan()->sites().size(); ++i) {
        const auto& s = g.civPlan()->sites()[i];
        for (size_t k = 1; k < s.history.size(); ++k) {
            const auto& prev = s.history[k - 1];
            const auto& ev = s.history[k];
            if (ev.kind == "grew") {
                ++grew;
                if (ev.epoch == s.founded) ++grewAtFounding;
                if (std::fabs(ev.value - prev.value * (1.0f + 0.114f * (0.5f + s.score))) > 1e-4f * std::max(1.0f, ev.value)) ++badGrow;
            } else if (ev.kind == "shock") {
                ++shocks;
                if (std::fabs(ev.value - prev.value * 0.55f) > 1e-4f * std::max(1.0f, ev.value)) ++badShock;
                const auto& t = h.civPlan()->sites()[i];
                for (const auto& te : t.history)
                    if (te.kind == "shock" && !(te.value < ev.value - 1e-6f)) ++harderNotLower;
            }
        }
    }
    std::printf("[ M3b ] ledger math: %d grew events (%d wrong, %d in the founding epoch), %d shocks (%d wrong, %d not lower at 90 %%)\n",
                grew, badGrow, grewAtFounding, shocks, badShock, harderNotLower);
    EXPECT_GT(grew, 1000); EXPECT_GT(shocks, 100);
    EXPECT_EQ(badGrow, 0); EXPECT_EQ(badShock, 0); EXPECT_EQ(grewAtFounding, 0); EXPECT_EQ(harderNotLower, 0);
}

// Audit 2026-10-22: the share is the lever — 0 empties nothing, 1 empties every village-and-below
// site founded before the shock, 0.5 about half; and ruins are no longer the youngest sites: with
// founding order a seeded hash, the hamlet/outpost ruins' founding epochs spread over the pre-shock
// epochs instead of all sitting at the last two.
TEST(CivHistoryTest, AbandonShareIsTheLeverAndRuinsAreNotJustTheYoungest) {
    int low = 0;
    std::map<int, int> ruinFounded;
    for (float share : {0.0f, 0.5f, 1.0f, 0.2f}) {
        WorldRecipe r = civRecipe(20260816u, 12);
        r.worldforge.civAbandonShare = share;
        WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
        g.applyRecipe(r);
        ASSERT_TRUE(g.civPlan());
        int ruins = 0; low = 0;
        for (const auto& s : g.civPlan()->sites()) {
            if (s.tier >= CivPlan::Village) ++low;
            if (s.state == CivPlan::Abandoned) { ++ruins; if (share == 0.2f && s.tier >= CivPlan::Hamlet) ++ruinFounded[s.founded]; }
        }
        std::printf("[ M3b ] civAbandonShare %.1f: %d ruins of %d village-and-below\n", share, ruins, low);
        if (share == 0.0f) EXPECT_EQ(ruins, 0);
        if (share == 1.0f) EXPECT_EQ(ruins, low);
        if (share == 0.5f) { EXPECT_GE(ruins, low * 45 / 100); EXPECT_LE(ruins, low * 55 / 100); }
    }
    std::printf("[ M3b ] hamlet/outpost ruins by founding epoch:");
    for (const auto& kv : ruinFounded) std::printf(" e%d:%d", kv.first, kv.second);
    std::printf("\n");
    EXPECT_GE(ruinFounded.size(), 4u) << "ruins come from fewer than four founding epochs (the youngest-only artefact)";
    EXPECT_GT(ruinFounded[0], 0) << "no ruin among the epoch-0 hamlets/outposts";
}

// Audit 2026-10-22: a cross-build pin. The civEpochs-0 plan must equal, site for site (cell, tier,
// x, z, score), the civ list the pre-M3b Release engine wrote for the same recipe
// (docs/evidence/worldforge_v2/m3a_l4_green3_civ.json, commit 77d320cf, 2026-10-22).
TEST(CivHistoryTest, EpochsZeroMatchesThePreM3bEngineRecord) {
    std::ifstream f("docs/evidence/worldforge_v2/m3a_l4_green3_civ.json");
    ASSERT_TRUE(f.good()) << "run from the repo root";
    nlohmann::json rec = nlohmann::json::parse(f);
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(civRecipe(20260816u, 0));
    ASSERT_TRUE(g.civPlan());
    const auto& sites = g.civPlan()->sites();
    ASSERT_EQ(sites.size(), rec["sites"].size());
    int mismatches = 0;
    for (size_t i = 0; i < sites.size(); ++i) {
        const auto& a = sites[i]; const auto& b = rec["sites"][i];
        if (std::string(CivPlan::tierName(a.tier)) != b["tier"].get<std::string>() || std::fabs(a.pos.x - b["x"].get<float>()) > 0.01f ||
            std::fabs(a.pos.y - b["z"].get<float>()) > 0.01f || std::fabs(a.score - b["score"].get<float>()) > 1e-5f) ++mismatches;
    }
    std::printf("[ M3b ] civEpochs 0 vs the pre-M3b engine record: %zu sites, %d mismatches\n", sites.size(), mismatches);
    EXPECT_EQ(mismatches, 0) << "M3b moved the civEpochs-0 plan away from M3a";
}

// Audit 2026-10-22: on the history path the box plan is still seeded by the LIVE civ sites inside
// the box — exactly those, position within a macro cell.
TEST(CivHistoryTest, BoxPlanIsSeededByTheLiveCivSitesAtTwelveEpochs) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(civRecipe(20260816u, 12));
    const CivPlan* civ = g.civPlan();
    const WorldForgePlan* box = g.worldForge();
    ASSERT_TRUE(civ && box);
    int liveInBox = 0, matched = 0;
    for (const auto& s : civ->sites())
        if (s.state != CivPlan::Abandoned && s.tier <= CivPlan::Hamlet && std::fabs(s.pos.x) < 16384.0f - 512.0f && std::fabs(s.pos.y) < 16384.0f - 512.0f) ++liveInBox;
    for (const auto& bs : box->sites())
        for (const auto& s : civ->sites())
            if (s.state != CivPlan::Abandoned && std::fabs(s.pos.x - bs.pos.x) <= 80.0f && std::fabs(s.pos.y - bs.pos.y) <= 80.0f) { ++matched; break; }
    std::printf("[ M3b ] box plan at 12 epochs: %zu sites, %d live civ sites inside the box, %d matched\n", box->sites().size(), liveInBox, matched);
    EXPECT_EQ(static_cast<int>(box->sites().size()), liveInBox);
    EXPECT_EQ(matched, static_cast<int>(box->sites().size()));
}

// Audit 2026-10-22: the shock edge. civEpochs 5 with the default shock epoch 9 clamps the shock to
// 5 (the last epoch), so the declined sites have no epoch left to be abandoned in: they END the
// arc as "declining" — allowed and stated (a ruin needs the epoch after the shock), never a silent
// no-shock.
TEST(CivHistoryTest, ShockAtTheLastEpochLeavesDecliningSitesNotRuins) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(civRecipe(20260816u, 5));
    ASSERT_TRUE(g.civPlan());
    const CivPlan& civ = *g.civPlan();
    EXPECT_EQ(g.getWorldForgeParams().clamped().civShockEpoch, 5);
    int shocked = 0, declining = 0, abandoned = 0;
    for (const auto& s : civ.sites()) {
        for (const auto& e : s.history) if (e.kind == "shock") { ++shocked; EXPECT_EQ(e.epoch, 5); }
        if (s.state == CivPlan::Declining) ++declining;
        if (s.state == CivPlan::Abandoned) ++abandoned;
    }
    std::printf("[ M3b ] shock at the last epoch (5): %d shocked, %d declining, %d abandoned\n", shocked, declining, abandoned);
    EXPECT_GT(shocked, 0);
    EXPECT_GT(declining, 0);
    EXPECT_EQ(abandoned, 0);
}
