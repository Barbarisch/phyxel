#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "core/Chunk.h"
#include "core/ChunkManager.h"
#include "core/SettlementBuildService.h"

using namespace Phyxel::Core;

// ============================================================================
// Terrain-aware settlement PLANNING, headless.
//
// This is the second thing the lift into phyxel_core bought. `plan()` reads the
// live world (grounding gate, buildability analysis, flattest-spine choice,
// dropping plots that straddle unbuildable ground) and then returns the heavy
// phases as unexecuted work units -- so the whole terrain-aware DECISION layer can
// be driven against a synthetic world with no engine, no Vulkan and no renderer.
// While this lived in editor/src/Application.cpp none of it had a test surface.
//
// SCOPE, stated up front so this is not read as more than it is: this covers
// PLANNING ONLY. The units are never run, because StructureGenerator::place() is
// not headless-testable (a prior finding, recorded in StructureGroundingTest), so
// nothing here says a terrain settlement is WALKABLE once stamped -- that remains
// an L4 question. What it does prove is that the terrain decisions are made, are
// deterministic, and degrade honestly instead of silently.
// ============================================================================

namespace {

// A headless world: `chunksX` x 1 x 1 chunks of solid terrain whose surface height
// is given by `topAt(worldX, worldZ)`. Same construction WaterManagerTest uses.
class HeadlessWorld {
public:
    HeadlessWorld(int chunksX, const std::function<int(int, int)>& topAt) {
        m_cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
        for (int cx = 0; cx < chunksX; ++cx) {
            auto owned = std::make_unique<Phyxel::Chunk>(glm::ivec3(cx, 0, 0));
            owned->initializeForLoading();
            for (int lx = 0; lx < 32; ++lx)
                for (int lz = 0; lz < 32; ++lz) {
                    const int wx = cx * 32 + lx;
                    const int top = topAt(wx, lz);
                    for (int y = 0; y <= top && y < 32; ++y)
                        owned->addCube(glm::ivec3(lx, y, lz));
                }
            m_cm.chunkMap[glm::ivec3(cx, 0, 0)] = owned.get();
            m_cm.chunks.push_back(std::move(owned));
        }
    }
    Phyxel::ChunkManager* cm() { return &m_cm; }

private:
    Phyxel::ChunkManager m_cm;
};

// THE RIG'S SITE (2026-11-10, chain 126): since M5b-1 the village is GROWN from the map to the program's band
// (min 6 lots) and refuses below it; the 48 x 28 site these tests used seats 5 on flat ground. The site is
// 96 x 28 on a 3-chunk world; the terrain functions are unchanged.
constexpr int kW = 96, kD = 28, kChunksX = 3;

SettlementBuildService::Plan planOn(HeadlessWorld& w, int W, int D, bool terrain,
                                    int seed = 3, const char* tier = "village") {
    nlohmann::json params = {{"era", "medieval"}, {"tier", tier}, {"seed", seed},
                             {"width", W}, {"depth", D}, {"terrain", terrain},
                             {"position", {{"x", 0}, {"y", 16}, {"z", 0}}}};
    SettlementBuildService::Deps deps;
    deps.chunkManager = w.cm();
    return SettlementBuildService::plan(params, deps);
}

}  // namespace

// ---------------------------------------------------------------------------
// The grounding gate: a settlement must refuse a site with holes in it. This is
// the gate that was added after the user found a village generated over thin air,
// and until now it had no headless test at all.
// ---------------------------------------------------------------------------
TEST(SettlementTerrainPlanTest, ASiteWithUngroundedColumnsIsRefused) {
    // Terrain everywhere EXCEPT a hole at x in [20,24], z in [10,14].
    HeadlessWorld w(kChunksX, [](int x, int z) {
        if (x >= 20 && x <= 24 && z >= 10 && z <= 14) return -1;   // no terrain at all
        return 16;
    });
    const auto plan = planOn(w, kW, kD, /*terrain=*/false);

    ASSERT_FALSE(plan.ok()) << "a site with a hole in it planned successfully";
    EXPECT_TRUE(plan.error.contains("ungrounded_columns"))
        << "the refusal must name HOW MANY columns lack terrain: " << plan.error.dump();
    EXPECT_EQ(plan.error.value("ungrounded_columns", 0), 25)
        << "the 5x5 hole should be counted exactly, not approximated";
    EXPECT_TRUE(plan.error.contains("first_ungrounded"))
        << "the refusal must point at WHERE, so the caller can go look";
    EXPECT_TRUE(plan.units.empty());
}

// ...and the documented override must actually override, or the gate is a wall
// rather than a default.
TEST(SettlementTerrainPlanTest, TheUngroundedOverrideIsHonoured) {
    HeadlessWorld w(kChunksX, [](int x, int z) {
        if (x >= 20 && x <= 24 && z >= 10 && z <= 14) return -1;
        return 16;
    });
    nlohmann::json params = {{"era", "medieval"}, {"tier", "village"}, {"seed", 3},
                             {"width", kW}, {"depth", kD}, {"allow_ungrounded", true},
                             {"position", {{"x", 0}, {"y", 16}, {"z", 0}}}};
    SettlementBuildService::Deps deps;
    deps.chunkManager = w.cm();
    const auto plan = SettlementBuildService::plan(params, deps);

    EXPECT_TRUE(plan.ok()) << "allow_ungrounded did not override the grounding gate: "
                           << plan.error.dump();
    EXPECT_FALSE(plan.units.empty());
}

// ---------------------------------------------------------------------------
// Terrain mode on FLAT ground must produce a full settlement -- the baseline that
// makes the steep-terrain degradation below meaningful rather than vacuous.
// ---------------------------------------------------------------------------
TEST(SettlementTerrainPlanTest, TerrainModeOnFlatGroundPlansAFullSettlement) {
    HeadlessWorld w(kChunksX, [](int, int) { return 16; });
    const auto plan = planOn(w, kW, kD, /*terrain=*/true);

    ASSERT_TRUE(plan.ok()) << plan.error.dump();
    EXPECT_GT(plan.settlement.value("buildings", 0), 0);
    EXPECT_EQ(plan.program.value("dropped_plots", -1), 0)
        << "plots were dropped as unbuildable on perfectly FLAT ground";
}

// ---------------------------------------------------------------------------
// The rig these tests used until 2026-11-10: a village on 48 x 28. Since M5b-1 the grown village refuses below the
// program's minimum; on this flat site it seats 5 of 6. A PIN of measured behaviour (chain 126), not
// red-before-green: the refusal carries the count, the aim and the buildable fraction (1.0 - the site is too
// SMALL, not unbuildable). Whether the M5a rows seated more on this rect was not measured.
// ---------------------------------------------------------------------------
TEST(SettlementTerrainPlanTest, AVillageSiteBelowItsMinimumIsRefusedWithTheCount) {
    HeadlessWorld w(2, [](int, int) { return 16; });
    const auto plan = planOn(w, 48, 28, /*terrain=*/true);
    ASSERT_FALSE(plan.ok()) << "the 48 x 28 village planned - the pin of chain 126 no longer holds (a change in how many lots the growth seats)";
    std::printf("[ terrain plan ] village 48 x 28 flat: REFUSED %s\n", plan.error.dump().c_str());
    EXPECT_EQ(plan.error.value("seated_lots", -1), 5);
    EXPECT_GE(plan.error.value("target_lots", -1), 6);
    EXPECT_DOUBLE_EQ(plan.error.value("buildable_fraction", -1.0), 1.0);
    EXPECT_TRUE(plan.units.empty());
}

// ---------------------------------------------------------------------------
// The real terrain behaviour: on ground too steep to build on, the planner must
// DEGRADE HONESTLY -- refuse with a reason, or drop plots and SAY SO -- never
// silently site buildings on cliffs.
// ---------------------------------------------------------------------------
TEST(SettlementTerrainPlanTest, ImpossibleTerrainIsRefusedWithAReasonNotSilentlyBuilt) {
    // A sawtooth: every column alternates between y=16 and y=30. Relief 14 per cell,
    // far past any buildable threshold -- nowhere is flat.
    HeadlessWorld w(kChunksX, [](int x, int z) { return ((x + z) % 2 == 0) ? 16 : 30; });
    const auto plan = planOn(w, kW, kD, /*terrain=*/true);

    EXPECT_FALSE(plan.ok())
        << "a settlement was planned on sawtooth cliffs - the buildability gate is not firing";
    EXPECT_TRUE(plan.error.contains("buildable_fraction"))
        << "the refusal must report HOW unbuildable the site was: " << plan.error.dump();
    EXPECT_LT(plan.error.value("buildable_fraction", 1.0), 0.5) << "a sawtooth of 14-cube steps reported as mostly buildable";
    EXPECT_TRUE(plan.units.empty()) << "a refused terrain plan handed back work units";
}

// A slope that is buildable in places must produce a settlement, and the count of
// plots dropped for unbuildability must be SURFACED (graceful degradation is only
// honest if it is reported).
// (RENAMED 2026-11-10 from PartlyBuildableTerrainSurfacesTheDroppedPlotCount: it no longer measures a dropped count)
TEST(SettlementTerrainPlanTest, PartlyBuildableTerrainSeatsNoLotOnTheRamp) {
    // Flat half (z < 16) and a steep ramp half (z >= 16) climbing 3 cubes per cell.
    // Both tiers (2026-11-10, chain 132): the village is REFUSED on this rig (5 of its minimum 6 fit the flat half), so
    // its arm proves the refusal's report; the hamlet (minimum 3) plans, so its arm runs the lot assertions.
    int planned = 0;
    for (const char* tier : {"village", "hamlet"}) {
        HeadlessWorld w(kChunksX, [](int x, int z) { return z < 16 ? 16 : 16 + (z - 16) * 3; });
        const auto plan = planOn(w, kW, kD, /*terrain=*/true, 3, tier);
        if (!plan.ok()) {
            // Acceptable outcome, but it still has to say why - and how far the growth got.
            std::printf("[ terrain plan ] half ramp, %s: REFUSED %s\n", tier, plan.error.dump().c_str());
            EXPECT_TRUE(plan.error.contains("buildable_fraction")) << plan.error.dump();
            EXPECT_LT(plan.error.value("buildable_fraction", 1.0), 0.75) << "half the site is a 3-per-cell ramp";
            EXPECT_TRUE(plan.error.contains("seated_lots")) << plan.error.dump();
            continue;
        }
        ++planned;
        // RESTATED 2026-11-10 for the grown map (after chain 126, stated in pm_chain132.txt): growth does not DROP a
        // lot on the ramp, it never SEATS one there - "dropped_plots" stays 0 by construction. The contract (nothing
        // stands on the ramp; a settlement that shrank for the terrain says so) is measured directly: no lot's rect
        // reaches a ramp row - with the flat CONTROL at the same seed seating at least one lot past that row, so the
        // assertion can fail - and the growth's aim (target_lots) is echoed beside the count.
        HeadlessWorld flat(kChunksX, [](int, int) { return 16; });
        const auto control = planOn(flat, kW, kD, /*terrain=*/true, 3, tier);
        ASSERT_TRUE(control.ok()) << control.error.dump();
        auto lotsOf = [](const SettlementBuildService::Plan& pl) { return pl.map.value("lots", nlohmann::json::array()); };
        auto pastRamp = [](const nlohmann::json& lots) {
            int n = 0;
            for (const auto& l : lots) if (l["rect"][1].get<int>() + l["rect"][3].get<int>() > 17) ++n;   // rect = [x, z, w, d]; z1 exclusive
            return n;
        };
        const auto lots = lotsOf(plan), controlLots = lotsOf(control);
        std::printf("[ terrain plan ] half ramp, %s: %zu lots, %d past row 16 (flat control %zu lots, %d past row 16), target %d\n", tier,
                    lots.size(), pastRamp(lots), controlLots.size(), pastRamp(controlLots), plan.program.value("target_lots", -1));
        ASSERT_GT(pastRamp(controlLots), 0) << "control broken: on flat ground no lot reaches the rows the ramp occupies";
        EXPECT_EQ(pastRamp(lots), 0) << "a lot's rect reaches the ramp (rows z >= 17 climb 3 per cell)";
        EXPECT_LE(lots.size(), controlLots.size());
        EXPECT_TRUE(plan.program.contains("target_lots")) << "the growth's aim must be echoed beside the seated count";
        EXPECT_GT(plan.settlement.value("buildings", 0), 0) << "the flat half should still host buildings";
    }
    EXPECT_GT(planned, 0) << "neither tier planned on the half ramp - the lot assertions never ran";
}

// ---------------------------------------------------------------------------
// Terrain planning must be deterministic in the seed -- the same contract the
// program echo advertises, now on a real (synthetic) landscape rather than the
// flat-plane path. Teeth: a different seed must differ, or the check is vacuous.
// ---------------------------------------------------------------------------
TEST(SettlementTerrainPlanTest, TerrainPlanningIsDeterministicInTheSeed) {
    auto mk = [](int seed) {
        HeadlessWorld w(kChunksX, [](int x, int z) { return 16 + ((x / 16) + (z / 16)) % 2; });
        return planOn(w, kW, kD, /*terrain=*/true, seed).queuedBuilds.dump();
    };
    const std::string a = mk(3), b = mk(3), c = mk(9);
    EXPECT_EQ(a, b) << "the same seed produced a different terrain settlement";
    EXPECT_NE(a, c) << "a different seed produced an IDENTICAL settlement - the seed is not "
                       "reaching the terrain path, so the determinism check above is vacuous";
}

// ---------------------------------------------------------------------------
// The site-prep units DO get planned once a world is present -- the complement of
// SettlementBuildServiceTest's world-free case, which asserts they are skipped and
// the skip is surfaced. Together these pin both halves of that branch.
// ---------------------------------------------------------------------------
TEST(SettlementTerrainPlanTest, WithAWorldTheSitePrepUnitsAreActuallyPlanned) {
    HeadlessWorld w(kChunksX, [](int, int) { return 16; });
    const auto plan = planOn(w, kW, kD, /*terrain=*/false);
    ASSERT_TRUE(plan.ok()) << plan.error.dump();

    EXPECT_FALSE(plan.settlement.value("site_prep_skipped", false))
        << "site prep reported as skipped even though a world was supplied";

    bool sawPrep = false, sawBuilding = false;
    int firstPrep = -1, firstBuilding = -1;
    for (size_t i = 0; i < plan.units.size(); ++i) {
        const std::string& l = plan.units[i].label;
        if (l == "clearing parcels" || l == "fencing parcels" ||
            l.rfind("grading", 0) == 0 || l.rfind("terracing", 0) == 0) {
            sawPrep = true;
            if (firstPrep < 0) firstPrep = (int)i;
        }
        if (l.rfind("building ", 0) == 0 && firstBuilding < 0) {
            sawBuilding = true;
            firstBuilding = (int)i;
        }
    }
    EXPECT_TRUE(sawPrep) << "no site-prep unit was planned even with a world present";
    ASSERT_TRUE(sawBuilding);
    EXPECT_LT(firstPrep, firstBuilding)
        << "site prep is ordered AFTER the first building - buildings would be seated on "
           "uncleared, ungraded ground (the invariant the handler only stated in a comment)";
}

// ---------------------------------------------------------------------------
// M0 (docs/WorldForgeV2Plan.md §5 M0): WATER is unbuildable. `analyzeSite` has had a
// `waterAt` predicate since SiteAnalysis landed, and plan() passed `{}` for it — so a
// creek bed or a lake margin inside the site rect was "buildable" and a plot could be
// planned straight across it (the F7 live plan put 40 order-1 creek crossings in the
// region; nothing stopped a lot from sitting on one). RED: with a band of water spans
// across a flat site, today a building straddles the band.
// ---------------------------------------------------------------------------
namespace {
// Mark columns x in [x0, x1] (world cubes, every z) as holding a thin water span on the
// surface — the chunk's own persisted "water as world data" record, what a loaded world
// carries. The band runs ACROSS the main street (which lies along x on this site), so the
// burgage plots on both sides of the street straddle it.
void floodBandX(HeadlessWorld& w, int chunksX, int x0, int x1, int surfaceY, int z0 = 0, int z1 = 31) {
    for (int cx = 0; cx < chunksX; ++cx) {
        Phyxel::Chunk* ch = w.cm()->getChunkAtFast(glm::ivec3(cx * 32, surfaceY, 0));
        ASSERT_NE(ch, nullptr);
        std::vector<Phyxel::Chunk::WaterSpanLocal> spans;
        for (int lx = 0; lx < 32; ++lx) {
            const int wx = cx * 32 + lx;
            if (wx < x0 || wx > x1) continue;
            for (int z = std::max(0, z0); z <= std::min(31, z1); ++z) {
                Phyxel::Chunk::WaterSpanLocal s;
                s.x = static_cast<uint8_t>(lx); s.z = static_cast<uint8_t>(z);
                s.bottom = static_cast<float>(surfaceY + 1); s.top = static_cast<float>(surfaceY + 1) + 0.6f;
                spans.push_back(s);
            }
        }
        ch->setWaterSpans(std::move(spans));
    }
}
// lots of the plan's MAP whose building footprint ([x, z, w, d], settlement-local = world on this rig) overlaps the box
int footprintsInBox(const SettlementBuildService::Plan& plan, int x0, int x1, int z0, int z1) {
    int n = 0;
    for (const auto& l : plan.map.value("lots", nlohmann::json::array())) {
        const auto& f = l["footprint"];
        const int fx = f[0].get<int>(), fz = f[1].get<int>(), fw = f[2].get<int>(), fd = f[3].get<int>();
        if (fx <= x1 && fx + fw - 1 >= x0 && fz <= z1 && fz + fd - 1 >= z0) ++n;
    }
    return n;
}
}  // namespace

// (RENAMED 2026-11-10 from CreekInsideFootprintIsNotBuildable: the water is a pond; the creek is the pin at the end)
TEST(SettlementTerrainPlanTest, WaterInsideFootprintIsNotBuildable) {
    // CONTROL: plan the dry flat site first and take the water from its FIRST lot's building footprint (its middle
    // two columns, over the footprint's own rows), so the water is where a planned building provably stands.
    // RIG CHANGE 2026-11-10 (after chain 132, stated in pm_chain133.txt): the water was a band across the WHOLE
    // depth; since M5b-1 the village's main street is routed and a site bisected by water is refused ("no dry
    // route for the main street") - pinned below as measured behaviour. The water here is a pond the street can pass.
    HeadlessWorld dry(kChunksX, [](int, int) { return 16; });
    const auto dryPlan = planOn(dry, kW, kD, /*terrain=*/true);
    ASSERT_TRUE(dryPlan.ok()) << dryPlan.error.dump();
    const auto dryLots = dryPlan.map.value("lots", nlohmann::json::array());
    ASSERT_FALSE(dryLots.empty());
    const auto& f0 = dryLots[0]["footprint"];
    const int fx = f0[0].get<int>(), fz = f0[1].get<int>(), fw = f0[2].get<int>(), fd = f0[3].get<int>();
    const int x0 = fx + fw / 2 - 1, x1 = fx + fw / 2, z0 = fz, z1 = fz + fd - 1;
    ASSERT_GT(footprintsInBox(dryPlan, x0, x1, z0, z1), 0)
        << "control broken: the water was taken from a planned building, it must stand in it";

    HeadlessWorld wet(kChunksX, [](int, int) { return 16; });
    floodBandX(wet, kChunksX, x0, x1, 16, z0, z1);
    const auto plan = planOn(wet, kW, kD, /*terrain=*/true);
    ASSERT_TRUE(plan.ok()) << plan.error.dump();
    std::printf("[ terrain plan ] pond x %d..%d z %d..%d: dry %zu lots, wet %zu lots, footprints in the pond %d\n", x0, x1, z0, z1,
                dryLots.size(), plan.map.value("lots", nlohmann::json::array()).size(), footprintsInBox(plan, x0, x1, z0, z1));
    EXPECT_EQ(footprintsInBox(plan, x0, x1, z0, z1), 0)
        << "a building was planned over the water x " << x0 << ".." << x1 << " z " << z0 << ".." << z1
        << " (analyzeSite received no water predicate)";
    // (the legacy "dropped_plots > 0" assertion is REMOVED 2026-11-10, stated in pm_chain132.txt: the grown map
    // seats the lot elsewhere instead of dropping it - nothing is dropped; the assertion above is the contract)
    EXPECT_GT(plan.settlement.value("buildings", 0), 0) << "the dry parts still host buildings";

    // A PIN of measured behaviour (chain 132), not red-before-green: water across the whole depth bisects the site
    // and the village is refused with the street's reason, not planned with a street through the water.
    HeadlessWorld cut(kChunksX, [](int, int) { return 16; });
    floodBandX(cut, kChunksX, x0, x1, 16);
    const auto cutPlan = planOn(cut, kW, kD, /*terrain=*/true);
    EXPECT_FALSE(cutPlan.ok());
    EXPECT_NE(cutPlan.error.value("error", std::string()).find("no dry route"), std::string::npos) << cutPlan.error.dump();
}

// ---------------------------------------------------------------------------
// M0 (docs/WorldForgeV2Plan.md §5 M0, "parcel canopy margin derived from the biome's flora
// pool"). F4b grounding pass read the tree library: ordinary oaks are 16 cubes wide (half 8),
// forge_enchanted_oak 23 (11.5), forge_jungle_l 19, forge_acacia_l 17, megaflora up to 52
// (26) - against a parcel-clearing margin that was the literal 4 and an orphan-canopy sweep
// margin of 8. A tree rooted 5 cubes outside a plot with an 11-cube crown overhung the lot.
// RED: today the plan echoes no margin at all (both numbers are literals in two units).
// ---------------------------------------------------------------------------
#include "core/ObjectTemplateManager.h"
#include "core/WorldGenerator.h"

TEST(SettlementTerrainPlanTest, CanopyMarginCoversTheBiomesWidestTree) {
    // the pure rule first
    EXPECT_EQ(SettlementBuildService::canopyMarginCubes({}, 4), 4);
    EXPECT_EQ(SettlementBuildService::canopyMarginCubes({8, 3}, 4), 8);
    EXPECT_EQ(SettlementBuildService::canopyMarginCubes({2, 3}, 4), 4);
    EXPECT_EQ(SettlementBuildService::canopyMarginCubes({12, 26, 9}, 4), 26);

    // Then the wiring: a streaming generator (its biomes carry the flora pools) + the loaded
    // nature library (the template manager knows every tree's half-footprint).
    Phyxel::ObjectTemplateManager templates(nullptr);
    templates.loadTemplates("resources/templates/nature");
    ASSERT_GT(templates.templateHalfFootprintColumns("forge_oak_l"), 0)
        << "rig: the nature library did not load headless";

    HeadlessWorld w(kChunksX, [](int, int) { return 16; });
    w.cm()->configureStreamingGeneration(true, Phyxel::WorldGenerator::GenerationType::Perlin, 20260816);
    Phyxel::WorldGenerator* gen = w.cm()->getStreamingGenerator();
    ASSERT_NE(gen, nullptr);
    // NOTE: configureStreamingGeneration replaces the generator but the headless chunks stay.
    // RIG CHANGE 2026-11-10 (after chain 132, stated in pm_chain133.txt): the village is refused on the generator's
    // ground under this rect (5 of its minimum 6, buildable fraction 1.0 - NOT explained); the margin under test is
    // the biome's, so the rig plans the hamlet, the tier the materials test below already uses.
    nlohmann::json params = {{"era", "medieval"}, {"tier", "hamlet"}, {"seed", 3},
                             {"width", kW}, {"depth", kD}, {"terrain", true},
                             {"position", {{"x", 0}, {"y", 16}, {"z", 0}}}};
    SettlementBuildService::Deps deps;
    deps.chunkManager = w.cm();
    deps.templates = &templates;
    const auto plan = SettlementBuildService::plan(params, deps);
    ASSERT_TRUE(plan.ok()) << plan.error.dump();

    // Expected: the widest crown the SITE biome can grow (pool + extra layers, pool mode), floor 4.
    const auto& biome = gen->getBiomes()[gen->sampleSurface(kW / 2, kD / 2).biomeIndex];
    std::vector<int> halves;
    auto add = [&](const std::vector<std::pair<std::string, int>>& items, const std::string& mode) {
        for (const auto& it : items) {
            const int hw = templates.templateHalfFootprintColumns(it.first);
            halves.push_back(hw > 0 ? hw : (mode == "procedural" ? templates.floraMarginColumns() : 0));
        }
    };
    add(biome.flora, biome.floraMode);
    for (const auto& L : biome.extraFloraLayers) add(L.items, L.mode);
    const int expected = SettlementBuildService::canopyMarginCubes(halves, 4);

    ASSERT_TRUE(plan.program.contains("parcel_clear_margin"))
        << "the plan must echo the derived parcel-clearing margin (today a literal 4 in the unit)";
    EXPECT_EQ(plan.program.value("parcel_clear_margin", -1), expected)
        << "biome " << biome.name << ": widest crown half-width of its pool is " << expected;
    EXPECT_GE(plan.program.value("canopy_sweep_margin", -1), 2 * expected)
        << "the orphan-canopy sweep must reach one more crown radius beyond the cleared band";
}

// ---------------------------------------------------------------------------
// M0 (docs/WorldForgeV2Plan.md §5 M0, "terrace/fence/path materials from the biome + program,
// not literals"). Terraces emitted "Dirt"/"Grass" in every biome (a snow or savanna village got
// temperate turf pads), fences were always "Log", cluster paths always "Cobblestone" while the
// tier program already names a street material. RED: the plan echoes none of these choices.
// ---------------------------------------------------------------------------
TEST(SettlementTerrainPlanTest, TerraceFenceAndPathMaterialsComeFromBiomeAndProgram) {
    HeadlessWorld w(2, [](int, int) { return 16; });
    w.cm()->configureStreamingGeneration(true, Phyxel::WorldGenerator::GenerationType::Perlin, 20260816);
    Phyxel::WorldGenerator* gen = w.cm()->getStreamingGenerator();
    ASSERT_NE(gen, nullptr);
    const auto& biome = gen->getBiomes()[gen->sampleSurface(24, 14).biomeIndex];

    nlohmann::json params = {{"era", "medieval"}, {"tier", "hamlet"}, {"seed", 3},
                             {"width", 48}, {"depth", 28}, {"terrain", true},
                             {"position", {{"x", 0}, {"y", 16}, {"z", 0}}}};
    SettlementBuildService::Deps deps;
    deps.chunkManager = w.cm();
    const auto plan = SettlementBuildService::plan(params, deps);
    ASSERT_TRUE(plan.ok()) << plan.error.dump();

    ASSERT_TRUE(plan.program.contains("terrace_top")) << "the plan must record the terrace materials it will emit";
    EXPECT_EQ(plan.program.value("terrace_top", ""), biome.surfaceMaterial)
        << "biome " << biome.name << ": a graded pad is resurfaced in the BIOME's surface, not literal Grass";
    EXPECT_EQ(plan.program.value("terrace_fill", ""), biome.subsurfaceMaterial)
        << "biome " << biome.name << ": fill below the pad is the biome's subsurface, not literal Dirt";
    EXPECT_EQ(plan.program.value("fence_material", ""), "Log")
        << "the tier program's fence material (data; medieval default Log)";
    // hamlet = cluster morphology: its door-to-door paths use the TIER's street material (Dirt)
    EXPECT_EQ(plan.program.value("path_material", ""), "Dirt")
        << "cluster paths must pave in the tier's street material, not literal Cobblestone";
}
